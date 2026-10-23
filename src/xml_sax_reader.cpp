#include "xml_sax_reader.hpp"
#include "xml_utils.hpp"
#include "xml_file_io.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include <sstream>
#include <unordered_set>

namespace duckdb {

// ─── SAXRecordAccumulator ───────────────────────────────────────────────────

static const std::vector<FieldOccurrence> EMPTY_OCCURRENCES;

// Forward declaration: defined later in this file, used by WidenSchemaForOutOfSampleValues above it.
static bool IsNullString(const std::string &text, const XMLSchemaOptions &options);

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
	field_has_children = false;
	field_text_run.clear();
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
		case '\r':
			// Match libxml2's DOM serializer. A literal CR in text content is
			// normalized to LF on the next parse (XML 1.0 §2.11), so it must be
			// escaped to round-trip. DOM (xmlNodeDump/xmlDocDumpMemory) emits
			// "&#13;"; without this the SAX path emitted a raw CR byte, so the
			// same node serialized to different bytes under DOM vs SAX — which
			// also silently breaks any downstream \s-based text processing.
			result += "&#13;";
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
		case '\r':
			// Attribute-value normalization (XML 1.0 §3.3.3) turns CR/LF/TAB into
			// spaces on parse, so libxml2's DOM serializer escapes all three in
			// attribute values. Mirror that here for DOM/SAX byte-parity.
			result += "&#13;";
			break;
		case '\n':
			result += "&#10;";
			break;
		case '\t':
			result += "&#9;";
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

// ─── Helper: read an attribute value from SAX2 startElementNs ───────────────

// Without entity substitution, libxml2 hands SAX2 attribute values with '&' re-encoded as "&#38;"
// (the other predefined entities arrive decoded) and leaves decoding to the consumer, as its own
// tree builder does. Decode it so SAX values match DOM.
static std::string SAXAttributeValue(const xmlChar **attributes, int i) {
	const char *value_start = reinterpret_cast<const char *>(attributes[i * 5 + 3]);
	const char *value_end = reinterpret_cast<const char *>(attributes[i * 5 + 4]);
	std::string value(value_start, value_end);
	static const std::string encoded_amp = "&#38;";
	size_t pos = value.find(encoded_amp);
	while (pos != std::string::npos) {
		value.replace(pos, encoded_amp.size(), "&");
		pos = value.find(encoded_amp, pos + 1);
	}
	return value;
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

// Move the direct child's pending text into its fragment. A run of only whitespace is indentation
// between child elements, which the DOM path drops too.
static void FlushFieldTextRun(SAXRecordAccumulator &acc) {
	if (acc.field_text_run.find_first_not_of(" \t\r\n") != std::string::npos) {
		acc.nested_xml += XmlEscapeText(acc.field_text_run);
	}
	acc.field_text_run.clear();
}

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

			// Extract attributes from the record element itself, unless attr_mode='discard'
			if (!sax_ctx->discard_attrs) {
				for (int i = 0; i < nb_attributes; i++) {
					const char *attr_localname = reinterpret_cast<const char *>(attributes[i * 5]);
					const char *attr_prefix_ptr = reinterpret_cast<const char *>(attributes[i * 5 + 1]);

					std::string attr_name;
					if (acc->namespace_mode == "keep" && attr_prefix_ptr != nullptr) {
						attr_name = std::string(attr_prefix_ptr) + ":" + attr_localname;
					} else {
						attr_name = attr_localname;
					}

					std::string attr_value = SAXAttributeValue(attributes, i);
					if (acc->current_attributes.emplace(attr_name, attr_value).second) {
						acc->attribute_order.push_back(attr_name);
					}
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
			acc->nested_xml.clear();
			acc->field_has_children = false;
			acc->field_text_run.clear();

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

					std::string attr_name;
					if (acc->namespace_mode == "keep" && attr_prefix_ptr != nullptr) {
						attr_name = std::string(attr_prefix_ptr) + ":" + attr_localname;
					} else {
						attr_name = attr_localname;
					}
					std::string attr_value = SAXAttributeValue(attributes, i);
					acc->current_field_attrs += " " + attr_name + "=\"" + XmlEscapeAttr(attr_value) + "\"";
				}
			}
		} else if (relative_depth > 1) {
			// Deeper nested element: accumulate raw XML
			if (relative_depth == 2) {
				FlushFieldTextRun(*acc);
			}
			acc->nested_depth = relative_depth;
			acc->field_has_children = true;
			acc->nested_xml += "<" + name;

			// Add attributes to raw XML (escape attribute values)
			for (int i = 0; i < nb_attributes; i++) {
				const char *attr_localname = reinterpret_cast<const char *>(attributes[i * 5]);
				std::string attr_value = SAXAttributeValue(attributes, i);
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
			// it has element children (acc->nested_xml, which includes its own text) OR carries its own attributes
			// (acc->current_field_attrs), so the rich-typing path reconstructs a STRUCT (@attrs plus
			// #text/children); otherwise it is plain text. own_attrs rides on the occurrence and is
			// spliced into the reconstructed open tag. Append in close order, capturing document order
			// across a field's text and XML occurrences.
			std::string own_attrs = std::move(acc->current_field_attrs);
			bool has_attrs = !own_attrs.empty();
			FlushFieldTextRun(*acc);
			bool is_xml = acc->field_has_children || has_attrs;
			std::string value;
			if (acc->field_has_children) {
				value = std::move(acc->nested_xml);
			} else if (has_attrs) {
				// Attribute-only (or attribute + text) child: the inner content is the element's text,
				// escaped so it stays well-formed when wrapped as <field own_attrs>text</field>.
				value = XmlEscapeText(
				    XMLSchemaInference::CleanTextContent(acc->current_text, sax_ctx->preserve_whitespace));
			} else {
				value = XMLSchemaInference::CleanTextContent(acc->current_text, sax_ctx->preserve_whitespace);
			}

			// Key the field by local name: read_xml names columns by local name in every namespace
			// mode, as DOM does. The prefix stays on nested fragments, which reparse against the
			// accumulated declarations.
			std::string field_name = reinterpret_cast<const char *>(localname);
			auto field_it = acc->current_fields.find(field_name);
			if (field_it == acc->current_fields.end()) {
				field_it = acc->current_fields.emplace(field_name, std::vector<FieldOccurrence>()).first;
				acc->field_order.push_back(field_name);
			}
			field_it->second.push_back({is_xml, std::move(value), std::move(own_attrs)});

			acc->current_text.clear();
			acc->current_element_name.clear();
			acc->current_field_attrs.clear();
			acc->nested_xml.clear();
			acc->field_has_children = false;
			acc->field_text_run.clear();
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
		// Direct child text: accumulate, and keep its place among any element children
		acc->current_text += text;
		acc->field_text_run += text;
	}
	// relative_depth == 0 means text directly in the record element (whitespace usually), ignore
}

// A CDATA section is a lexical device rather than content: its text IS the element's text, which
// is why scalar columns unwrap it. But when SAX is reconstructing a nested XML fragment it has to
// match the DOM serializer byte for byte, and xmlNodeDump keeps the section framing -- so preserve
// it there (issue #158). At relative_depth <= 1 the value is plain text and the markers must not
// appear, matching xmlNodeGetContent on the DOM side.
void SAXCdataBlock(void *ctx, const xmlChar *ch, int len) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing || acc->state != SAXAccumulatorState::IN_RECORD) {
		return;
	}

	if (acc->current_depth - acc->record_depth <= 1) {
		// Direct child (or record-level) text: the section contributes its content, unwrapped.
		SAXCharacters(ctx, ch, len);
		return;
	}

	std::string text(reinterpret_cast<const char *>(ch), static_cast<size_t>(len));
	// A CDATA section cannot contain "]]>": close and reopen around it, as libxml2's serializer
	// does, so the reconstructed fragment still parses back to this same text.
	std::string framed;
	framed.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == ']' && i + 2 < text.size() && text[i + 1] == ']' && text[i + 2] == '>') {
			framed += "]]]]><![CDATA[>";
			i += 2;
		} else {
			framed += text[i];
		}
	}
	acc->nested_xml += "<![CDATA[" + framed + "]]>";
}

// libxml2 hands the comment text without its delimiters. DOM keeps comments inside a serialized
// fragment, so SAX must too; in scalar text a comment contributes nothing (xmlNodeGetContent skips
// it), which is what both paths already did. Registering this handler is what stops comments from
// disappearing from streamed fragments entirely (issue #158).
void SAXComment(void *ctx, const xmlChar *value) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing || acc->state != SAXAccumulatorState::IN_RECORD || !value) {
		return;
	}
	if (acc->current_depth - acc->record_depth <= 1) {
		return;
	}
	acc->nested_xml += "<!--" + std::string(reinterpret_cast<const char *>(value)) + "-->";
}

// Same contract for processing instructions: preserved inside a nested fragment, ignored in scalar
// text (issue #158). A PI with no data serializes as "<?target?>".
void SAXProcessingInstruction(void *ctx, const xmlChar *target, const xmlChar *data) {
	auto *sax_ctx = static_cast<SAXCallbackContext *>(ctx);
	auto *acc = sax_ctx->accumulator;

	if (sax_ctx->stop_parsing || acc->state != SAXAccumulatorState::IN_RECORD || !target) {
		return;
	}
	if (acc->current_depth - acc->record_depth <= 1) {
		return;
	}
	std::string pi = "<?" + std::string(reinterpret_cast<const char *>(target));
	if (data && *data) {
		pi += " " + std::string(reinterpret_cast<const char *>(data));
	}
	pi += "?>";
	acc->nested_xml += pi;
}

// ─── SAXStreamReader ────────────────────────────────────────────────────────

xmlSAXHandler SAXStreamReader::CreateSAXHandler() {
	xmlSAXHandler handler {};

	handler.initialized = XML_SAX2_MAGIC;
	handler.startElementNs = SAXStartElementNs;
	handler.endElementNs = SAXEndElementNs;
	handler.characters = SAXCharacters;
	handler.cdataBlock = SAXCdataBlock;
	// Without these two, libxml2 simply drops comments and processing instructions, and a nested
	// fragment reconstructed from SAX loses nodes the DOM path keeps (issue #158).
	handler.comment = SAXComment;
	handler.processingInstruction = SAXProcessingInstruction;

	return handler;
}

std::string SAXStreamReader::DescribeParseError(xmlParserCtxtPtr ctx) {
	if (!ctx) {
		return "SAX parser context was null";
	}
	std::string msg;
	const xmlError *err = xmlCtxtGetLastError(ctx);
	if (err && err->message) {
		msg.assign(err->message);
		// libxml2 error messages carry a trailing newline; trim it.
		while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
			msg.pop_back();
		}
		// domain/code/level pin down WHICH libxml2 check fired (e.g. a size/limit
		// guard vs. a well-formedness error) when the human message is terse.
		msg += " (libxml2 domain=" + std::to_string(err->domain) + " code=" + std::to_string(err->code) +
		       " level=" + std::to_string(static_cast<int>(err->level));
		if (err->line > 0) {
			msg += " line=" + std::to_string(err->line);
		}
		msg += ")";
	} else {
		msg = "libxml2 reported a parse failure with no detail — the input may exceed a built-in libxml2 limit "
		      "(element depth, text-node size, etc.) that XML_PARSE_HUGE relaxes";
	}
	// Byte offset consumed so far: a value near a 2GiB/4GiB boundary points at a
	// 32-bit limit; a small/specific offset points at a structural (format) issue
	// in a particular record rather than scale.
	long consumed = xmlByteConsumed(ctx);
	if (consumed >= 0) {
		msg += "; parsed ~" + std::to_string(consumed) + " bytes before failing";
	}
	return msg;
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
	auto file_handle = OpenXMLFile(fs, filename);

	// Create push parser context (fail-closed entity loader: refuse external DTD/entity fetch)
	XMLUtils::EnsureSecureParsing();
	xmlParserCtxtPtr parser_ctx = xmlCreatePushParserCtxt(&handler, &ctx, nullptr, 0, filename.c_str());
	if (!parser_ctx) {
		throw IOException("Could not create SAX push parser context for '%s'", filename);
	}

	// Match DOM: no XML_PARSE_RECOVER. Malformed XML must error (or be skipped
	// via ignore_errors by the caller), not silently recover into partial rows.
	// XML_PARSE_HUGE: lift libxml2's built-in limits (element depth, text-node
	// length, and other accumulators) that otherwise abort a large streaming
	// parse. Required for multi-GB inputs; harmless for small ones.
	xmlCtxtUseOptions(parser_ctx, XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET | XML_PARSE_HUGE);

	char buffer[SAX_CHUNK_SIZE];

	while (!ctx.stop_parsing) {
		auto bytes_read = file_handle->Read(buffer, SAX_CHUNK_SIZE);
		if (bytes_read == 0) {
			break;
		}

		int result = xmlParseChunk(parser_ctx, buffer, static_cast<int>(bytes_read), 0);
		if (result != 0) {
			std::string detail = DescribeParseError(parser_ctx);
			xmlFreeParserCtxt(parser_ctx);
			throw IOException("SAX parsing error in file '%s': %s", filename, detail);
		}
	}

	// Reaching max_rows stops the read mid-document. Terminating the parse there would make libxml2
	// report the unclosed elements as malformed, so an intentional stop skips the final chunk.
	if (!ctx.stop_parsing) {
		int final_result = xmlParseChunk(parser_ctx, nullptr, 0, 1 /* terminate */);
		if (final_result != 0) {
			std::string detail = DescribeParseError(parser_ctx);
			xmlFreeParserCtxt(parser_ctx);
			throw IOException("SAX parsing error in file '%s': %s", filename, detail);
		}
	}
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
	std::string xml;
	xml += "<root";
	xml += records.back().BuildNamespaceDeclarations();
	xml += ">";

	for (const auto &record : records) {
		// Emit record-level attributes as real attributes, in document order, so DOM inference
		// applies the same attr_mode naming (e.g. 'prefixed') and typing as it does on real files
		// (skip when attr_mode='discard' to match DOM behavior)
		xml += "<record";
		if (options.attr_mode != "discard") {
			for (const auto &attr_name : record.attribute_order) {
				// Skip child element attributes (contain a dot)
				if (attr_name.find('.') != std::string::npos) {
					continue;
				}
				xml += " ";
				xml += attr_name;
				xml += "=\"";
				xml += XmlEscapeAttr(record.GetAttribute(attr_name));
				xml += "\"";
			}
		}
		xml += ">";

		// Emit each field's occurrences in document order: text payloads escaped, nested-XML
		// fragments emitted structurally so DOM inference reconstructs the STRUCT / LIST shape.
		for (const auto &field_name : record.field_order) {
			for (const auto &occ : record.GetOccurrences(field_name)) {
				// occ.own_attrs is pre-escaped and only populated when attr_mode != 'discard', so the
				// synthetic element carries the child's own attributes and DOM inference types it as a
				// STRUCT (@attrs + #text/children) — matching the non-streaming path.
				xml += "<";
				xml += field_name;
				xml += occ.own_attrs;
				xml += ">";
				if (occ.is_xml) {
					xml += occ.payload;
				} else {
					xml += XmlEscapeText(occ.payload);
				}
				xml += "</";
				xml += field_name;
				xml += ">";
			}
		}

		xml += "</record>";
	}

	xml += "</root>";
	// The sampled records are fully serialized. Release them before inference parses the synthetic
	// document, so the sample is not held twice alongside its DOM.
	records.clear();
	records.shrink_to_fit();

	// Use existing schema inference on the synthetic XML
	XMLSchemaOptions infer_options = options;
	// Force the record element to "record" since that's what we built
	infer_options.record_element = "record";

	auto columns = XMLSchemaInference::InferSchemaForMerge(xml, infer_options);

	// sample_size <= 0 opts into a full-file verify pass that widens any column whose out-of-sample
	// values don't fit the type inferred from the sample. (In SAX mode a non-positive sample_size
	// otherwise just capped the window at 50 — see InferSchemaFromStream's sample_count.)
	if (options.sample_size <= 0) {
		WidenSchemaForOutOfSampleValues(fs, filename, options, columns);
	}

	return columns;
}

// True for the scalar types ConvertToValue can REJECT (i.e. that XmlUncastableValue guards). VARCHAR
// accepts anything; BOOLEAN maps unknowns to NULL without erroring; STRUCT/LIST/MAP/ARRAY take a
// different extraction path. Only these can need widening.
//
// EXCLUDING BOOLEAN IS LOAD-BEARING, do not "fix" it by adding BOOLEAN here: the widen test treats a
// NULL result from ConvertToValuePublic (under ignore_errors) as a cast failure, but BOOLEAN returns
// NULL for an unrecognized literal WITHOUT going through XmlUncastableValue — so adding BOOLEAN would
// make every unrecognized boolean silently widen the column to VARCHAR.
static bool IsWidenableScalar(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::HUGEINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
	case LogicalTypeId::UBIGINT:
	case LogicalTypeId::UHUGEINT:
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
	case LogicalTypeId::DATE:
	case LogicalTypeId::TIME:
	case LogicalTypeId::TIME_TZ:
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_NS:
	case LogicalTypeId::TIMESTAMP_TZ:
		return true;
	default:
		return false;
	}
}

// True for a column the sample saw only empty elements for (SQLNULL, or a list of it). Any content
// outside the sample is new evidence, so it widens to VARCHAR, the type such a column had before
// SQLNULL deferred the decision to the multi-file merge.
static bool IsUnknownType(const LogicalType &type) {
	if (type.id() == LogicalTypeId::LIST) {
		return IsUnknownType(ListType::GetChildType(type));
	}
	return type.id() == LogicalTypeId::SQLNULL;
}

static LogicalType WidenUnknownToVarchar(const LogicalType &type) {
	if (type.id() == LogicalTypeId::LIST) {
		return LogicalType::LIST(WidenUnknownToVarchar(ListType::GetChildType(type)));
	}
	return LogicalType::VARCHAR;
}

void SAXStreamReader::WidenSchemaForOutOfSampleValues(FileSystem &fs, const std::string &filename,
                                                      const XMLSchemaOptions &options,
                                                      std::vector<XMLColumnInfo> &columns) {
	std::vector<bool> widen(columns.size(), false);

	// THE EARLY RETURN THAT USED TO LIVE HERE IS GONE, deliberately. It skipped the extra file
	// read when no column could widen. Discovery (#184, below) has no such precondition: a child
	// element whose first occurrence is beyond the sample window is undiscovered no matter what
	// the sampled columns are typed as, so the read is always worth making. `sample_size <= 0`
	// already means "look at every value", so paying for one full stream here is the contract,
	// not a regression.

	// #184: a child element first appearing BEYOND the sample window was absent from the schema
	// entirely, so querying it was a bind error ("Could not find key") rather than a NULL column.
	// This pass already streams every record at bind time, before the output schema is fixed, so
	// discovery costs a name comparison per field and no extra I/O.
	//
	// ELEMENT FIELDS ONLY. A late ATTRIBUTE is a separate case: attribute naming depends on
	// attr_mode, and `attribute_order` carries dotted child-element attributes that would need
	// their own handling. Out of scope here rather than half-done.
	// A SET for membership, a VECTOR for order. The first version scanned both containers
	// linearly, which was the wrong trade in THIS function specifically: it runs only when
	// sample_size <= 0, and that promises a pass over every record of a file of any size. So the
	// cost was O(records * fields * columns) on exactly the inputs that make it expensive --
	// 1M records with 50 fields and 200 columns is 10^10 string compares. The simpler code was
	// cheaper to read and quadratically more expensive to run.
	std::unordered_set<std::string> known;
	known.reserve(columns.size() * 2);
	for (const auto &c : columns) {
		known.insert(c.name);
	}
	std::vector<std::string> discovered; // first-seen order, so the appended schema is stable

	// ignore_errors so ConvertToValuePublic returns a typed NULL on an uncastable value instead of
	// throwing; combined with the empty/nullstr pre-filter below, a NULL result means "cast failed".
	XMLSchemaOptions verify_opts = options;
	verify_opts.ignore_errors = true;

	SAXRecordAccumulator accumulator;
	accumulator.namespace_mode = options.namespaces;
	if (!options.record_element.empty()) {
		std::string tag = options.record_element;
		if (tag.size() >= 2 && tag[0] == '/' && tag[1] == '/') {
			tag = tag.substr(2);
		}
		accumulator.record_tag = tag;
	}

	std::vector<SAXRecordAccumulator> completed;
	SAXCallbackContext ctx;
	ctx.accumulator = &accumulator;
	ctx.max_rows = 0; // unlimited: we must see every record, not just a sample
	ctx.rows_completed = 0;
	ctx.stop_parsing = false;
	ctx.completed_records = &completed;
	ctx.preserve_whitespace = options.preserve_whitespace;
	ctx.discard_attrs = (options.attr_mode == "discard");

	xmlSAXHandler handler = CreateSAXHandler();
	auto file_handle = OpenXMLFile(fs, filename);
	XMLUtils::EnsureSecureParsing();
	xmlParserCtxtPtr parser_ctx = xmlCreatePushParserCtxt(&handler, &ctx, nullptr, 0, filename.c_str());
	if (!parser_ctx) {
		throw IOException("Could not create SAX push parser context for '%s'", filename);
	}
	xmlCtxtUseOptions(parser_ctx, XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET | XML_PARSE_HUGE);

	// Test each buffered record's candidate columns, then drop the batch (bounded memory).
	auto test_and_clear = [&]() {
		for (auto &rec : completed) {
			for (const auto &field_name : rec.field_order) {
				// insert() reports whether it was new, so one lookup does both jobs
				if (known.insert(field_name).second) {
					discovered.push_back(field_name);
				}
			}
			for (idx_t i = 0; i < columns.size(); i++) {
				if (widen[i] || columns[i].is_attribute) {
					continue;
				}
				if (IsUnknownType(columns[i].type)) {
					for (const auto &occ : rec.GetOccurrences(columns[i].name)) {
						if (occ.is_xml || (!occ.payload.empty() && !IsNullString(occ.payload, options))) {
							widen[i] = true;
							break;
						}
					}
					continue;
				}
				if (!IsWidenableScalar(columns[i].type)) {
					continue;
				}
				for (const auto &occ : rec.GetOccurrences(columns[i].name)) {
					if (occ.is_xml || occ.payload.empty() || IsNullString(occ.payload, options)) {
						continue;
					}
					Value v = XMLSchemaInference::ConvertToValuePublic(occ.payload, columns[i].type, verify_opts,
					                                                   columns[i].winning_datetime_format);
					if (v.IsNull()) {
						widen[i] = true;
						break;
					}
				}
			}
		}
		completed.clear();
	};

	char buffer[SAX_CHUNK_SIZE];
	while (!ctx.stop_parsing) {
		auto bytes_read = file_handle->Read(buffer, SAX_CHUNK_SIZE);
		if (bytes_read == 0) {
			break;
		}
		int result = xmlParseChunk(parser_ctx, buffer, static_cast<int>(bytes_read), 0);
		if (result != 0) {
			std::string detail = DescribeParseError(parser_ctx);
			xmlFreeParserCtxt(parser_ctx);
			throw IOException("SAX parsing error in file '%s': %s", filename, detail);
		}
		test_and_clear();
	}
	int final_result = xmlParseChunk(parser_ctx, nullptr, 0, 1 /* terminate */);
	if (final_result != 0) {
		std::string detail = DescribeParseError(parser_ctx);
		xmlFreeParserCtxt(parser_ctx);
		throw IOException("SAX parsing error in file '%s': %s", filename, detail);
	}
	xmlFreeParserCtxt(parser_ctx);
	test_and_clear(); // drain the final batch

	for (idx_t i = 0; i < columns.size(); i++) {
		if (widen[i]) {
			columns[i].type =
			    IsUnknownType(columns[i].type) ? WidenUnknownToVarchar(columns[i].type) : LogicalType::VARCHAR;
			columns[i].winning_datetime_format.clear();
		}
	}

	// VARCHAR, deliberately. A column discovered here never appeared in the sampled synthetic
	// document, so there is no inferred type to widen FROM -- the widen machinery above cannot
	// help. Probing these values for a narrower type would put a second concern inside a function
	// whose comments already warn about exactly that (see the BOOLEAN note on IsWidenableScalar).
	// VARCHAR is never wrong here, only never narrow, and a caller who wants narrower has
	// `columns :=`.
	for (const auto &name : discovered) {
		columns.emplace_back(name, LogicalType::VARCHAR);
	}
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
				value = XMLSchemaInference::ExtractValueFromXmlFragment(col_name, occs.front().payload, col_type,
				                                                        options, ns_decls, occs.front().own_attrs);
			} else {
				// Text-only occurrence: wrap escaped text so the extractor yields a non-NULL struct
				// shell (fields NULL), matching DOM, rather than a fully-NULL struct. An empty element
				// takes the same path, and nullstr applies to the struct's scalar fields, as in DOM.
				value = XMLSchemaInference::ExtractValueFromXmlFragment(col_name, XmlEscapeText(occs.front().payload),
				                                                        col_type, options, ns_decls);
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
				if (child_is_complex) {
					// Text inside a complex-typed list: wrap so ExtractValueFromXmlFragment can
					// surface it as a #text-bearing struct (NULL for missing fields), applying nullstr
					// to the struct's scalar fields as DOM does. Escape XML special chars first so
					// '&', '<', '>' do not break the synthetic wrapper.
					list_vals.push_back(XMLSchemaInference::ExtractValueFromXmlFragment(
					    col_name, XmlEscapeText(item), child_type, options, ns_decls, own_attrs));
				} else {
					list_vals.push_back(
					    XMLSchemaInference::ConvertToValuePublic(item, child_type, options, datetime_fmt));
				}
			};
			auto append_xml = [&](const std::string &frag, const std::string &own_attrs) {
				if (child_is_complex) {
					list_vals.push_back(XMLSchemaInference::ExtractValueFromXmlFragment(col_name, frag, child_type,
					                                                                    options, ns_decls, own_attrs));
				} else if (child_type.id() == LogicalTypeId::VARCHAR) {
					// Scalar-typed list with an XML item: surface the serialized fragment.
					list_vals.push_back(Value(frag));
				} else {
					// A typed scalar item captured as XML is a wrapper of items (<l><i>1</i><i>2</i></l>)
					// or an element carrying its own attributes. Reparse and extract as DOM does.
					XMLSchemaInference::AppendListItemsFromXmlFragment(col_name, frag, child_type, options, ns_decls,
					                                                   own_attrs, datetime_fmt, list_vals);
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
				} else {
					// The element had element children or its own attributes. Reparse it with those
					// attributes and extract as DOM does: a VARCHAR column gets the serialized children,
					// and otherwise the text content is converted, so an empty attribute-only element
					// reads NULL.
					value = XMLSchemaInference::ExtractValueFromXmlFragment(col_name, occ.payload, col_type, options,
					                                                        ns_decls, occ.own_attrs, datetime_fmt);
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
