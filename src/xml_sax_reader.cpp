#include "xml_sax_reader.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include <sstream>

namespace duckdb {

// ─── SAXRecordAccumulator ───────────────────────────────────────────────────

static const std::vector<FieldOccurrence> EMPTY_OCCURRENCES;

void SAXRecordAccumulator::Reset() {
	state = SAXAccumulatorState::SEEKING_RECORD;
	current_fields.clear();
	current_attributes.clear();
	field_order.clear();
	attribute_order.clear();
	current_text.clear();
	current_element_name.clear();
	current_field_attrs.clear();
	nested_xml.clear();
	nested_depth = 0;
	row_ready = false;
	// Note: element_stack, current_depth, record_depth, record_tag, namespace_mode,
	// namespace_declarations are NOT reset — they persist across records
}

const std::vector<FieldOccurrence> &SAXRecordAccumulator::GetOccurrences(const std::string &name) const {
	auto it = current_fields.find(name);
	if (it != current_fields.end()) {
		return it->second;
	}
	return EMPTY_OCCURRENCES;
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

std::string SAXRecordAccumulator::BuildNamespaceDeclarations() const {
	std::string result;
	for (const auto &ns : namespace_declarations) {
		result += " xmlns:" + ns.first + "=\"" + XmlEscapeAttr(ns.second) + "\"";
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

	// In keep mode, record each prefixed namespace declaration so reparsed nested-XML fragments can
	// resolve their prefixes. Skip the default namespace (null prefix): extraction matches by local
	// name, so only prefixes need resolving.
	if (acc->namespace_mode == "keep") {
		for (int i = 0; i < nb_namespaces; i++) {
			const xmlChar *ns_prefix = namespaces[i * 2];
			const xmlChar *ns_uri = namespaces[i * 2 + 1];
			if (ns_prefix != nullptr && ns_uri != nullptr) {
				acc->namespace_declarations[reinterpret_cast<const char *>(ns_prefix)] =
				    reinterpret_cast<const char *>(ns_uri);
			}
		}
	}

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
				if (acc->current_attributes.emplace(attr_name, attr_value).second) {
					acc->attribute_order.push_back(attr_name);
				}
			}
		}
	} else if (acc->state == SAXAccumulatorState::IN_RECORD) {
		int relative_depth = acc->current_depth - acc->record_depth;

		if (relative_depth == 1) {
			// Direct child of record: start text accumulation
			acc->current_text.clear();
			acc->current_element_name = name;
			acc->nested_depth = 0;
			acc->current_field_attrs.clear();

			// Serialize this direct child's OWN attributes so they can be spliced into the field
			// element's open tag during reconstruction (schema inference + row build). Without this an
			// attribute-only child such as <CustomFunctionReference id=".." UUID=".."/> loses its
			// attributes under SAX streaming, while the DOM path keeps them. Held in current_field_attrs
			// until the element closes (children, if any, arrive in between). Skipped under
			// attr_mode='discard' to match DOM, where attributes are dropped entirely.
			if (!sax_ctx->discard_attrs) {
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
					acc->current_field_attrs += " " + attr_name + "=\"" + XmlEscapeAttr(attr_value) + "\"";
				}
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
			// Closing a direct child of the record. The child is captured as a nested-XML fragment when
			// it has element children (acc->nested_xml) OR carries its own attributes
			// (acc->current_field_attrs), so the rich-typing path reconstructs a STRUCT (@attrs plus
			// #text/children); otherwise it is plain text. own_attrs rides on the occurrence and is
			// spliced into the reconstructed open tag. Append in close order, capturing document order
			// across a field's text and XML occurrences.
			std::string own_attrs = std::move(acc->current_field_attrs);
			bool has_attrs = !own_attrs.empty();
			bool is_xml = !acc->nested_xml.empty() || has_attrs;
			std::string value;
			if (!acc->nested_xml.empty()) {
				value = std::move(acc->nested_xml);
				acc->nested_xml.clear();
			} else if (has_attrs) {
				// Attribute-only (or attribute + text) child: the inner content is the element's text,
				// escaped so it stays well-formed when wrapped as <field own_attrs>text</field>.
				value = XmlEscapeText(
				    XMLSchemaInference::CleanTextContent(acc->current_text, sax_ctx->preserve_whitespace));
			} else {
				value = XMLSchemaInference::CleanTextContent(acc->current_text, sax_ctx->preserve_whitespace);
			}

			auto field_it = acc->current_fields.find(name);
			if (field_it == acc->current_fields.end()) {
				field_it = acc->current_fields.emplace(name, std::vector<FieldOccurrence>()).first;
				acc->field_order.push_back(name);
			}
			field_it->second.push_back({is_xml, std::move(value), std::move(own_attrs)});

			acc->current_text.clear();
			acc->current_element_name.clear();
			acc->current_field_attrs.clear();
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
	ctx.discard_attrs = (options.attr_mode == "discard");

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
	// This lets us reuse the existing DOM-based InferSchema. Declare any prefixed namespaces on the
	// synthetic root so prefixed nested elements parse under strict (non-recovery) inference.
	// namespace_declarations accumulates monotonically across the parse and is never reset, so the
	// last record's snapshot is the union of every declaration seen while sampling.
	std::ostringstream xml;
	xml << "<root" << records.back().BuildNamespaceDeclarations() << ">";

	for (const auto &record : records) {
		// Emit record-level attributes as real attributes, in document order, so DOM inference
		// applies the same attr_mode naming (e.g. 'prefixed') and typing as it does on real files
		// (skip when attr_mode='discard' to match DOM behavior)
		xml << "<record";
		if (options.attr_mode != "discard") {
			for (const auto &attr_name : record.attribute_order) {
				// Skip child element attributes (contain a dot)
				if (attr_name.find('.') != std::string::npos) {
					continue;
				}
				xml << " " << attr_name << "=\"" << XmlEscapeAttr(record.GetAttribute(attr_name)) << "\"";
			}
		}
		xml << ">";

		// Emit each field's occurrences in document order: text payloads escaped, nested-XML
		// fragments emitted structurally so DOM inference reconstructs the STRUCT / LIST shape.
		for (const auto &field_name : record.field_order) {
			for (const auto &occ : record.GetOccurrences(field_name)) {
				// occ.own_attrs is pre-escaped and only populated when attr_mode != 'discard', so the
				// synthetic element carries the child's own attributes and DOM inference types it as a
				// STRUCT (@attrs + #text/children) — matching the non-streaming path.
				xml << "<" << field_name << occ.own_attrs << ">";
				xml << (occ.is_xml ? occ.payload : XmlEscapeText(occ.payload));
				xml << "</" << field_name << ">";
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

	// Namespace declarations to splice into reparsed fragments so prefixed names resolve (empty
	// unless namespaces:='keep'). Built once and reused across columns.
	const std::string ns_decls = accumulator.BuildNamespaceDeclarations();

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

		// Attributes are accumulated under their bare XML names; in attr_mode='prefixed' the
		// column name carries attr_prefix, so strip it before lookup.
		const std::string attr_name = XMLSchemaInference::AttributeNameFromColumn(col_name, options);
		if (is_attribute || accumulator.HasAttribute(attr_name)) {
			// Attribute value
			std::string attr_val = accumulator.GetAttribute(attr_name);
			if (attr_val.empty()) {
				value = Value(); // NULL
			} else if (IsNullString(attr_val, options)) {
				value = Value(); // NULL
			} else {
				value = XMLSchemaInference::ConvertToValuePublic(attr_val, col_type, options, datetime_fmt);
			}
		} else if (col_type.id() == LogicalTypeId::STRUCT) {
			// STRUCT column: a non-LIST struct implies a single occurrence. If it carried element
			// children or its own attributes it was captured as inner XML; re-parse and dispatch through
			// the DOM extractor. occ.own_attrs is appended to the wrapper open tag (via the extra_attrs
			// parameter, which is spliced there verbatim) so attribute-only children yield their @attr fields.
			const auto &occs = accumulator.GetOccurrences(col_name);
			if (occs.empty()) {
				value = Value(col_type); // typed NULL
			} else if (occs.front().is_xml) {
				value = XMLSchemaInference::ExtractValueFromXmlFragment(
				    col_name, occs.front().payload, col_type, options, ns_decls, occs.front().own_attrs);
			} else {
				// Text-only occurrence: wrap escaped text so the extractor yields a non-NULL struct
				// shell (fields NULL), matching DOM, rather than a fully-NULL struct.
				const std::string &text = occs.front().payload;
				if (text.empty() || IsNullString(text, options)) {
					value = Value(col_type); // typed NULL
				} else {
					value = XMLSchemaInference::ExtractValueFromXmlFragment(col_name, XmlEscapeText(text), col_type,
					                                                        options, ns_decls);
				}
			}
		} else if (col_type.id() == LogicalTypeId::LIST) {
			// LIST column. Iterate the field's occurrences in document order, dispatching each to the
			// text or XML appender by its kind, so a list mixing bare-text and nested-XML siblings
			// keeps its original sequence.
			auto child_type = ListType::GetChildType(col_type);
			const bool child_is_complex =
			    (child_type.id() == LogicalTypeId::STRUCT || child_type.id() == LogicalTypeId::LIST);

			std::vector<Value> list_vals;

			// own_attrs (pre-escaped, empty unless the element carried attributes) is appended to the
			// wrapper open tag via the extra_attrs parameter so list items keep their @attr fields.
			auto append_text = [&](const std::string &item, const std::string &own_attrs) {
				if (IsNullString(item, options)) {
					list_vals.push_back(Value(child_type));
				} else if (child_is_complex) {
					// Text inside a complex-typed list: wrap so ExtractValueFromXmlFragment can
					// surface it as a #text-bearing struct (NULL for missing fields). Escape XML
					// special chars first so '&', '<', '>' do not break the synthetic wrapper.
					list_vals.push_back(XMLSchemaInference::ExtractValueFromXmlFragment(
					    col_name, XmlEscapeText(item), child_type, options, ns_decls, own_attrs));
				} else {
					list_vals.push_back(
					    XMLSchemaInference::ConvertToValuePublic(item, child_type, options, datetime_fmt));
				}
			};
			auto append_xml = [&](const std::string &frag, const std::string &own_attrs) {
				if (child_is_complex) {
					list_vals.push_back(XMLSchemaInference::ExtractValueFromXmlFragment(
					    col_name, frag, child_type, options, ns_decls, own_attrs));
				} else if (child_type.id() == LogicalTypeId::VARCHAR) {
					// Scalar-typed list with an XML item: surface the serialized fragment.
					list_vals.push_back(Value(frag));
				} else {
					list_vals.push_back(Value(child_type));
				}
			};

			for (const auto &occ : accumulator.GetOccurrences(col_name)) {
				if (occ.is_xml) {
					append_xml(occ.payload, occ.own_attrs);
				} else {
					append_text(occ.payload, occ.own_attrs);
				}
			}

			if (list_vals.empty()) {
				value = Value(LogicalType::LIST(child_type)); // empty list
			} else {
				value = Value::LIST(child_type, std::move(list_vals));
			}
		} else {
			// Scalar value (possibly widened to VARCHAR).
			const auto &occs = accumulator.GetOccurrences(col_name);
			if (occs.empty()) {
				value = Value(); // NULL
			} else if (occs.size() == 1) {
				const auto &occ = occs.front();
				if (!occ.is_xml) {
					if (occ.payload.empty() || IsNullString(occ.payload, options)) {
						value = Value(); // NULL
					} else {
						value = XMLSchemaInference::ConvertToValuePublic(occ.payload, col_type, options, datetime_fmt);
					}
				} else if (col_type.id() == LogicalTypeId::VARCHAR) {
					// Widened to VARCHAR but the source element had element children: surface the fragment.
					value = Value(occ.payload);
				} else {
					value = Value(); // NULL
				}
			} else if (col_type.id() == LogicalTypeId::VARCHAR) {
				// Widened to VARCHAR but the element repeated: concatenate occurrences in document
				// order so none are dropped. Text items get wrapped in their element tag; XML items
				// are already serialized fragments.
				std::string serialized;
				for (const auto &occ : occs) {
					if (occ.is_xml) {
						serialized += occ.payload;
					} else {
						serialized += "<" + col_name + ">" + XmlEscapeText(occ.payload) + "</" + col_name + ">";
					}
				}
				value = serialized.empty() ? Value() : Value(serialized);
			} else {
				value = Value(); // NULL
			}
		}

		row.push_back(std::move(value));
	}

	return row;
}

} // namespace duckdb
