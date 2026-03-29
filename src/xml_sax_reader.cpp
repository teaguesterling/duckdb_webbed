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

// ─── Helper: UTF-8-safe whitespace trimming (matches CleanTextContent) ──────

static std::string TrimWhitespace(const std::string &text) {
	auto is_ascii_space = [](unsigned char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
	};
	auto lstart = std::find_if_not(text.begin(), text.end(),
	                               [&](char c) { return is_ascii_space(static_cast<unsigned char>(c)); });
	auto rend = std::find_if_not(text.rbegin(), text.rend(), [&](char c) {
		            return is_ascii_space(static_cast<unsigned char>(c));
	            }).base();
	if (lstart >= rend) {
		return "";
	}
	return std::string(lstart, rend);
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
			// Closing a direct child of the record
			std::string value;
			if (!acc->nested_xml.empty()) {
				// If we accumulated nested XML, use that instead
				value = acc->nested_xml;
				acc->nested_xml.clear();
			} else {
				// Trim whitespace to match DOM's CleanTextContent behavior
				value = TrimWhitespace(acc->current_text);
			}

			// Check if this field already has a value (repeated element -> list)
			auto it = acc->current_values.find(name);
			if (it != acc->current_values.end()) {
				// Move existing value to list and add new value
				auto &list = acc->current_lists[name];
				if (list.empty()) {
					list.push_back(it->second);
				}
				list.push_back(value);
				acc->current_values.erase(it);
			} else {
				auto list_it = acc->current_lists.find(name);
				if (list_it != acc->current_lists.end()) {
					// Already a list, append
					list_it->second.push_back(value);
				} else {
					// First occurrence, store as scalar
					acc->current_values[name] = value;
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
		} else if (col_type.id() == LogicalTypeId::LIST) {
			// LIST column: use accumulated list values
			const auto &list_values = accumulator.GetListValues(col_name);
			if (list_values.empty()) {
				// Check scalar values too (single element that should be a list)
				std::string scalar = accumulator.GetValue(col_name);
				if (scalar.empty()) {
					value = Value(LogicalType::LIST(ListType::GetChildType(col_type)));
					// Empty list
				} else if (IsNullString(scalar, options)) {
					value = Value(); // NULL
				} else {
					// Single value as a one-element list
					auto child_type = ListType::GetChildType(col_type);
					auto child_val =
					    XMLSchemaInference::ConvertToValuePublic(scalar, child_type, options, datetime_fmt);
					std::vector<Value> list_vals;
					list_vals.push_back(std::move(child_val));
					value = Value::LIST(child_type, std::move(list_vals));
				}
			} else {
				auto child_type = ListType::GetChildType(col_type);
				std::vector<Value> converted_list;
				converted_list.reserve(list_values.size());
				for (const auto &item : list_values) {
					if (IsNullString(item, options)) {
						converted_list.push_back(Value());
					} else {
						converted_list.push_back(
						    XMLSchemaInference::ConvertToValuePublic(item, child_type, options, datetime_fmt));
					}
				}
				value = Value::LIST(child_type, std::move(converted_list));
			}
		} else {
			// Scalar value
			std::string text = accumulator.GetValue(col_name);
			if (text.empty()) {
				value = Value(); // NULL
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
