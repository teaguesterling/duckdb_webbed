#include "xml_sax_reader.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include <sstream>

namespace duckdb {

// ─── SAXRecordAccumulator ───────────────────────────────────────────────────

static const std::vector<std::string> EMPTY_LIST;

void SAXRecordAccumulator::Reset() {
	state = SAXAccumulatorState::SEEKING_RECORD;
	current_values.clear();
	current_lists.clear();
	current_xml_values.clear();
	current_xml_lists.clear();
	current_attributes.clear();
	current_text.clear();
	current_element_name.clear();
	nested_xml.clear();
	nested_depth = 0;
	row_ready = false;
	// Note: element_stack, current_depth, record_depth, record_tag, namespace_mode
	// are NOT reset — they persist across records
}

std::string SAXRecordAccumulator::GetValue(const std::string &name) const {
	auto it = current_values.find(name);
	if (it != current_values.end()) {
		return it->second;
	}
	return "";
}

const std::vector<std::string> &SAXRecordAccumulator::GetListValues(const std::string &name) const {
	auto it = current_lists.find(name);
	if (it != current_lists.end()) {
		return it->second;
	}
	return EMPTY_LIST;
}

std::string SAXRecordAccumulator::GetXmlValue(const std::string &name) const {
	auto it = current_xml_values.find(name);
	if (it != current_xml_values.end()) {
		return it->second;
	}
	return "";
}

const std::vector<std::string> &SAXRecordAccumulator::GetXmlListValues(const std::string &name) const {
	auto it = current_xml_lists.find(name);
	if (it != current_xml_lists.end()) {
		return it->second;
	}
	return EMPTY_LIST;
}

bool SAXRecordAccumulator::HasAttribute(const std::string &name) const {
	return current_attributes.find(name) != current_attributes.end();
}

std::string SAXRecordAccumulator::GetAttribute(const std::string &name) const {
	auto it = current_attributes.find(name);
	if (it != current_attributes.end()) {
		return it->second;
	}
	return "";
}

// ─── Helper: escape XML special characters in text content ──────────────────

static std::string XmlEscapeText(const std::string &text) {
	std::string result;
	result.reserve(text.size());
	for (char c : text) {
		switch (c) {
		case '&':
			result += "&amp;";
			break;
		case '<':
			result += "&lt;";
			break;
		case '>':
			result += "&gt;";
			break;
		default:
			result += c;
			break;
		}
	}
	return result;
}

static std::string XmlEscapeAttr(const std::string &text) {
	std::string result;
	result.reserve(text.size());
	for (char c : text) {
		switch (c) {
		case '&':
			result += "&amp;";
			break;
		case '<':
			result += "&lt;";
			break;
		case '>':
			result += "&gt;";
			break;
		case '"':
			result += "&quot;";
			break;
		default:
			result += c;
			break;
		}
	}
	return result;
}

// ─── Helper: resolve element name based on namespace mode ───────────────────

static std::string ResolveElementName(const xmlChar *localname, const xmlChar *prefix,
                                      const std::string &namespace_mode) {
	std::string name = reinterpret_cast<const char *>(localname);
	if (namespace_mode == "keep" && prefix != nullptr) {
		name = std::string(reinterpret_cast<const char *>(prefix)) + ":" + name;
	}
	return name;
}

// ─── Helper: check if element matches the record tag ────────────────────────

static bool MatchesRecordTag(const std::string &element_name, const std::string &record_tag) {
	if (record_tag.empty()) {
		return false;
	}
	// Strip any leading "//" from XPath-style record element specification
	std::string tag = record_tag;
	if (tag.size() >= 2 && tag[0] == '/' && tag[1] == '/') {
		tag = tag.substr(2);
	}
	return element_name == tag;
}

// ─── SAX2 Callbacks ─────────────────────────────────────────────────────────

void SAXStartElementNs(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI,
                       int nb_namespaces, const xmlChar **namespaces, int nb_attributes, int nb_defaulted,
                       const xmlChar **attributes) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing) {
		return;
	}

	std::string name = ResolveElementName(localname, prefix, acc->namespace_mode);
	acc->current_depth++;
	acc->element_stack.push_back(name);

	if (acc->state == SAXAccumulatorState::SEEKING_RECORD) {
		// Determine if this element is a record element
		bool is_record = false;

		if (!acc->record_tag.empty()) {
			// Explicit record tag specified
			is_record = MatchesRecordTag(name, acc->record_tag);
		} else {
			// Auto-detect: records are depth==2 children of root
			// depth 1 = root, depth 2 = record children
			is_record = (acc->current_depth == 2);
		}

		if (is_record) {
			acc->state = SAXAccumulatorState::IN_RECORD;
			acc->record_depth = acc->current_depth;
			acc->nested_depth = 0;

			// Extract attributes from the record element itself
			for (int i = 0; i < nb_attributes; i++) {
				const char *attr_localname = reinterpret_cast<const char *>(attributes[i * 5]);
				const char *attr_prefix_ptr = reinterpret_cast<const char *>(attributes[i * 5 + 1]);
				const char *value_start = reinterpret_cast<const char *>(attributes[i * 5 + 3]);
				const char *value_end = reinterpret_cast<const char *>(attributes[i * 5 + 4]);

				std::string attr_name;
				if (acc->namespace_mode == "keep" && attr_prefix_ptr != nullptr) {
					attr_name = std::string(attr_prefix_ptr) + ":" + attr_localname;
				} else {
					attr_name = attr_localname;
				}

				std::string attr_value(value_start, value_end);
				acc->current_attributes[attr_name] = attr_value;
			}
		}
	} else if (acc->state == SAXAccumulatorState::IN_RECORD) {
		int relative_depth = acc->current_depth - acc->record_depth;

		if (relative_depth == 1) {
			// Direct child of record: start text accumulation
			acc->current_text.clear();
			acc->current_element_name = name;
			acc->nested_depth = 0;

			// Extract attributes from child elements too
			for (int i = 0; i < nb_attributes; i++) {
				const char *attr_localname = reinterpret_cast<const char *>(attributes[i * 5]);
				const char *attr_prefix_ptr = reinterpret_cast<const char *>(attributes[i * 5 + 1]);
				const char *value_start = reinterpret_cast<const char *>(attributes[i * 5 + 3]);
				const char *value_end = reinterpret_cast<const char *>(attributes[i * 5 + 4]);

				std::string attr_name;
				if (acc->namespace_mode == "keep" && attr_prefix_ptr != nullptr) {
					attr_name = std::string(attr_prefix_ptr) + ":" + attr_localname;
				} else {
					attr_name = attr_localname;
				}
				std::string attr_value(value_start, value_end);
				// Store child attributes with element.attribute naming
				acc->current_attributes[name + "." + attr_name] = attr_value;
			}
		} else if (relative_depth > 1) {
			// Deeper nested element: accumulate raw XML
			acc->nested_depth = relative_depth;
			acc->nested_xml += "<" + name;

			// Add attributes to raw XML (escape attribute values)
			for (int i = 0; i < nb_attributes; i++) {
				const char *attr_localname = reinterpret_cast<const char *>(attributes[i * 5]);
				const char *value_start = reinterpret_cast<const char *>(attributes[i * 5 + 3]);
				const char *value_end = reinterpret_cast<const char *>(attributes[i * 5 + 4]);
				std::string attr_value(value_start, value_end);
				acc->nested_xml += " " + std::string(attr_localname) + "=\"" + XmlEscapeAttr(attr_value) + "\"";
			}
			acc->nested_xml += ">";
		}
	}
}

void SAXEndElementNs(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing) {
		acc->current_depth--;
		if (!acc->element_stack.empty()) {
			acc->element_stack.pop_back();
		}
		return;
	}

	std::string name = ResolveElementName(localname, prefix, acc->namespace_mode);

	if (acc->state == SAXAccumulatorState::IN_RECORD) {
		int relative_depth = acc->current_depth - acc->record_depth;

		if (relative_depth == 0) {
			// Closing the record element itself — record is complete
			acc->state = SAXAccumulatorState::RECORD_COMPLETE;
			acc->row_ready = true;
			sax_ctx->rows_completed++;

			// Collect the completed record immediately (before next element starts)
			if (sax_ctx->completed_records) {
				sax_ctx->completed_records->push_back(*acc);
				acc->Reset();
			}

			if (sax_ctx->max_rows > 0 && sax_ctx->rows_completed >= sax_ctx->max_rows) {
				sax_ctx->stop_parsing = true;
			}
		} else if (relative_depth == 1) {
			// Closing a direct child of the record. If we accumulated nested XML (the child has
			// element children), store it in the XML-value map so rich-typing can re-parse it.
			// Otherwise treat the value as text.
			bool is_xml = !acc->nested_xml.empty();
			std::string value;
			if (is_xml) {
				value = acc->nested_xml;
				acc->nested_xml.clear();
			} else {
				value = XMLSchemaInference::CleanTextContent(acc->current_text, sax_ctx->preserve_whitespace);
			}

			auto &scalar_map = is_xml ? acc->current_xml_values : acc->current_values;
			auto &list_map = is_xml ? acc->current_xml_lists : acc->current_lists;

			// Repeated element -> list. Promote existing scalar to a list, or append to existing list.
			auto it = scalar_map.find(name);
			if (it != scalar_map.end()) {
				auto &list = list_map[name];
				if (list.empty()) {
					list.push_back(it->second);
				}
				list.push_back(value);
				scalar_map.erase(it);
			} else {
				auto list_it = list_map.find(name);
				if (list_it != list_map.end()) {
					list_it->second.push_back(value);
				} else {
					scalar_map[name] = value;
				}
			}

			acc->current_text.clear();
			acc->current_element_name.clear();
			acc->nested_depth = 0;
		} else {
			// Closing a deeper nested element: append closing tag to raw XML
			acc->nested_xml += "</" + name + ">";
		}
	}

	acc->current_depth--;
	if (!acc->element_stack.empty()) {
		acc->element_stack.pop_back();
	}
}

void SAXCharacters(void *ctx, const xmlChar *ch, int len) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing || acc->state != SAXAccumulatorState::IN_RECORD) {
		return;
	}

	std::string text(reinterpret_cast<const char *>(ch), static_cast<size_t>(len));

	int relative_depth = acc->current_depth - acc->record_depth;
	if (relative_depth > 1) {
		// Inside nested element: accumulate into raw XML (escape special chars
		// since SAX delivers decoded text but we're reconstructing XML)
		acc->nested_xml += XmlEscapeText(text);
	} else if (relative_depth == 1) {
		// Direct child text: accumulate
		acc->current_text += text;
	}
	// relative_depth == 0 means text directly in the record element (whitespace usually), ignore
}

void SAXCdataBlock(void *ctx, const xmlChar *ch, int len) {
	// Delegate to Characters handler
	SAXCharacters(ctx, ch, len);
}

// ─── SAXStreamReader ────────────────────────────────────────────────────────

xmlSAXHandler SAXStreamReader::CreateSAXHandler() {
	xmlSAXHandler handler {};

	handler.initialized = XML_SAX2_MAGIC;
	handler.startElementNs = SAXStartElementNs;
	handler.endElementNs = SAXEndElementNs;
	handler.characters = SAXCharacters;
	handler.cdataBlock = SAXCdataBlock;

	return handler;
}

std::vector<SAXRecordAccumulator> SAXStreamReader::ReadRecords(FileSystem &fs, const std::string &filename,
                                                               const XMLSchemaOptions &options, idx_t max_rows) {
	std::vector<SAXRecordAccumulator> results;

	SAXRecordAccumulator accumulator;
	accumulator.namespace_mode = options.namespaces;

	// Configure record tag matching
	if (!options.record_element.empty()) {
		std::string tag = options.record_element;
		// Strip "//" prefix if present
		if (tag.size() >= 2 && tag[0] == '/' && tag[1] == '/') {
			tag = tag.substr(2);
		}
		// Reject XPath tokens that SAX can't evaluate
		if (tag.find_first_of("/@*[]") != std::string::npos) {
			throw InvalidInputException(
			    "SAX reader: record_element '%s' contains XPath syntax not supported in SAX mode. "
			    "Use a simple element name (e.g., 'item') or set streaming:=false.",
			    options.record_element);
		}
		accumulator.record_tag = tag;
	}

	SAXCallbackContext ctx;
	ctx.accumulator = &accumulator;
	ctx.max_rows = max_rows;
	ctx.rows_completed = 0;
	ctx.stop_parsing = false;
	ctx.completed_records = &results;
	ctx.preserve_whitespace = options.preserve_whitespace;

	xmlSAXHandler handler = CreateSAXHandler();

	// Open file via DuckDB FileSystem (supports S3, HTTP, VFS, etc.)
	auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);

	// Create push parser context
	xmlParserCtxtPtr parser_ctx = xmlCreatePushParserCtxt(&handler, &ctx, nullptr, 0, filename.c_str());
	if (!parser_ctx) {
		throw IOException("Could not create SAX push parser context for '%s'", filename);
	}

	// Configure parser options (thread-safe, no global state modification)
	xmlCtxtUseOptions(parser_ctx, XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);

	char buffer[SAX_CHUNK_SIZE];

	while (!ctx.stop_parsing) {
		auto bytes_read = file_handle->Read(buffer, SAX_CHUNK_SIZE);
		if (bytes_read == 0) {
			break;
		}

		int result = xmlParseChunk(parser_ctx, buffer, static_cast<int>(bytes_read), 0);
		if (result != 0 && !options.ignore_errors) {
			xmlFreeParserCtxt(parser_ctx);
			throw IOException("SAX parsing error in file '%s'", filename);
		}
	}

	// Finalize parsing
	xmlParseChunk(parser_ctx, nullptr, 0, 1 /* terminate */);
	xmlFreeParserCtxt(parser_ctx);

	return results;
}

std::vector<XMLColumnInfo> SAXStreamReader::InferSchemaFromStream(FileSystem &fs, const std::string &filename,
                                                                  const XMLSchemaOptions &options) {
	// Accumulate sample records using SAX
	idx_t sample_count = static_cast<idx_t>(options.sample_size > 0 ? options.sample_size : 50);
	auto records = ReadRecords(fs, filename, options, sample_count);

	if (records.empty()) {
		return {};
	}

	// Build a synthetic XML document from accumulated records
	// This lets us reuse the existing DOM-based InferSchema
	std::ostringstream xml;
	xml << "<root>";

	for (const auto &record : records) {
		xml << "<record>";

		// Emit record-level attributes as elements for schema inference
		// (skip when attr_mode='discard' to match DOM behavior)
		if (options.attr_mode != "discard") {
			for (const auto &attr : record.current_attributes) {
				// Skip child element attributes (contain a dot)
				if (attr.first.find('.') != std::string::npos) {
					continue;
				}
				xml << "<" << attr.first << ">" << XmlEscapeText(attr.second) << "</" << attr.first << ">";
			}
		}

		// Emit scalar values (escape text to produce valid XML)
		for (const auto &val : record.current_values) {
			xml << "<" << val.first << ">" << XmlEscapeText(val.second) << "</" << val.first << ">";
		}

		// Emit list values as repeated elements
		for (const auto &list : record.current_lists) {
			for (const auto &item : list.second) {
				xml << "<" << list.first << ">" << XmlEscapeText(item) << "</" << list.first << ">";
			}
		}

		// Emit nested-XML values structurally so DOM inference reconstructs STRUCT shape.
		for (const auto &val : record.current_xml_values) {
			xml << "<" << val.first << ">" << val.second << "</" << val.first << ">";
		}

		// Emit nested-XML lists structurally as repeated elements.
		for (const auto &list : record.current_xml_lists) {
			for (const auto &item : list.second) {
				xml << "<" << list.first << ">" << item << "</" << list.first << ">";
			}
		}

		xml << "</record>";
	}

	xml << "</root>";

	// Use existing schema inference on the synthetic XML
	XMLSchemaOptions infer_options = options;
	// Force the record element to "record" since that's what we built
	infer_options.record_element = "record";

	return XMLSchemaInference::InferSchema(xml.str(), infer_options);
}

// Helper: check if a value is a null string (replicates the pattern from xml_schema_inference.cpp)
static bool IsNullString(const std::string &text, const XMLSchemaOptions &options) {
	for (const auto &ns : options.null_strings) {
		if (text == ns) {
			return true;
		}
	}
	return false;
}

std::vector<Value> SAXStreamReader::AccumulatorToRow(const SAXRecordAccumulator &accumulator,
                                                     const std::vector<std::string> &column_names,
                                                     const std::vector<LogicalType> &column_types,
                                                     const XMLSchemaOptions &options,
                                                     const std::vector<std::string> &column_datetime_formats,
                                                     const std::vector<XMLColumnInfo> &inferred_schema) {
	std::vector<Value> row;
	row.reserve(column_names.size());

	for (idx_t i = 0; i < column_names.size(); i++) {
		const auto &col_name = column_names[i];
		const auto &col_type = column_types[i];
		std::string datetime_fmt;
		if (i < column_datetime_formats.size()) {
			datetime_fmt = column_datetime_formats[i];
		} else if (i < inferred_schema.size()) {
			datetime_fmt = inferred_schema[i].winning_datetime_format;
		}

		Value value;

		// Check if this column is an attribute (from inferred schema metadata)
		bool is_attribute = false;
		if (i < inferred_schema.size()) {
			is_attribute = inferred_schema[i].is_attribute;
		}

		if (is_attribute || accumulator.HasAttribute(col_name)) {
			// Attribute value
			std::string attr_val = accumulator.GetAttribute(col_name);
			if (attr_val.empty()) {
				value = Value(); // NULL
			} else if (IsNullString(attr_val, options)) {
				value = Value(); // NULL
			} else {
				value = XMLSchemaInference::ConvertToValuePublic(attr_val, col_type, options, datetime_fmt);
			}
		} else if (col_type.id() == LogicalTypeId::STRUCT) {
			// STRUCT column: the child element had element children, captured as inner XML during
			// streaming. Re-parse and dispatch through the DOM extractor.
			std::string inner_xml = accumulator.GetXmlValue(col_name);
			if (inner_xml.empty()) {
				value = Value(col_type); // typed NULL
			} else {
				value = XMLSchemaInference::ExtractValueFromXmlFragment(col_name, inner_xml, col_type, options);
			}
		} else if (col_type.id() == LogicalTypeId::LIST) {
			// LIST column. Combine text-shaped items (current_lists / current_values) and
			// nested-XML items (current_xml_lists / current_xml_values). Document order between
			// text and XML items is not preserved (the accumulator stores them in parallel maps);
			// items within a single map keep their relative order.
			auto child_type = ListType::GetChildType(col_type);
			const bool child_is_complex = (child_type.id() == LogicalTypeId::STRUCT ||
			                               child_type.id() == LogicalTypeId::LIST);

			const auto &text_items = accumulator.GetListValues(col_name);
			std::string text_single = accumulator.GetValue(col_name);
			const auto &xml_items = accumulator.GetXmlListValues(col_name);
			std::string xml_single = accumulator.GetXmlValue(col_name);

			std::vector<Value> list_vals;

			auto append_text = [&](const std::string &item) {
				if (IsNullString(item, options)) {
					list_vals.push_back(Value(child_type));
				} else if (child_is_complex) {
					// Text inside a complex-typed list: wrap so ExtractValueFromXmlFragment can
					// surface it as a #text-bearing struct (NULL for missing fields).
					list_vals.push_back(
					    XMLSchemaInference::ExtractValueFromXmlFragment(col_name, item, child_type, options));
				} else {
					list_vals.push_back(
					    XMLSchemaInference::ConvertToValuePublic(item, child_type, options, datetime_fmt));
				}
			};
			auto append_xml = [&](const std::string &frag) {
				if (child_is_complex) {
					list_vals.push_back(
					    XMLSchemaInference::ExtractValueFromXmlFragment(col_name, frag, child_type, options));
				} else if (child_type.id() == LogicalTypeId::VARCHAR) {
					// Scalar-typed list with an XML item: surface the serialized fragment.
					list_vals.push_back(Value(frag));
				} else {
					list_vals.push_back(Value(child_type));
				}
			};

			if (!text_items.empty()) {
				for (const auto &item : text_items) {
					append_text(item);
				}
			} else if (!text_single.empty()) {
				append_text(text_single);
			}
			if (!xml_items.empty()) {
				for (const auto &frag : xml_items) {
					append_xml(frag);
				}
			} else if (!xml_single.empty()) {
				append_xml(xml_single);
			}

			if (list_vals.empty()) {
				value = Value(LogicalType::LIST(child_type)); // empty list
			} else {
				value = Value::LIST(child_type, std::move(list_vals));
			}
		} else {
			// Scalar value. If the column was widened to VARCHAR but the source element had element
			// children, the data is in the XML map; surface it as the serialized fragment.
			std::string text = accumulator.GetValue(col_name);
			if (text.empty()) {
				std::string inner_xml = accumulator.GetXmlValue(col_name);
				if (!inner_xml.empty() && col_type.id() == LogicalTypeId::VARCHAR) {
					value = Value(inner_xml);
				} else {
					value = Value(); // NULL
				}
			} else if (IsNullString(text, options)) {
				value = Value(); // NULL
			} else {
				value = XMLSchemaInference::ConvertToValuePublic(text, col_type, options, datetime_fmt);
			}
		}

		row.push_back(std::move(value));
	}

	return row;
}

} // namespace duckdb
