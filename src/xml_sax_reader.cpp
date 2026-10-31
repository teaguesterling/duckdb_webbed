#include "xml_sax_reader.hpp"
#include "xml_utils.hpp"
#include "xml_file_io.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <libxml/SAX2.h>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace duckdb {

// libxml2's tree builder takes the parser context as its callback argument, so the stream state
// rides on the context's _private slot.
static XMLRecordStream::State &StreamState(void *ctx) {
	return *static_cast<XMLRecordStream::State *>(static_cast<xmlParserCtxtPtr>(ctx)->_private);
}

// Compare an element name as read_xml resolves it: prefixed in namespaces := 'keep', local otherwise.
static bool NameEquals(const std::string &name, const xmlChar *localname, const xmlChar *prefix, bool keep_prefix) {
	auto local = reinterpret_cast<const char *>(localname);
	if (keep_prefix && prefix) {
		auto pre = reinterpret_cast<const char *>(prefix);
		auto pre_len = strlen(pre);
		return name.size() > pre_len && name.compare(0, pre_len, pre) == 0 && name[pre_len] == ':' &&
		       name.compare(pre_len + 1, std::string::npos, local) == 0;
	}
	return name == local;
}

static bool IsRecordStart(const XMLRecordStream::State &s, const xmlChar *localname, const xmlChar *prefix) {
	if (s.root_is_record) {
		return s.depth == 1;
	}
	if (!s.record_tag.empty()) {
		return NameEquals(s.record_tag, localname, prefix, s.keep_prefix);
	}
	if (!s.root_element.empty()) {
		return s.root_depth > 0 && !s.root_done && s.depth == s.root_depth + 1;
	}
	return s.depth == 2;
}

// libxml2 parses an entity's content once through these callbacks into a list it keeps as the entity's children, so
// that content is built in full and never selected or freed as records.
static bool InEntity(void *ctx) {
	return static_cast<xmlParserCtxtPtr>(ctx)->inputNr > 1;
}

static void StreamStartElement(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI,
                               int nb_namespaces, const xmlChar **namespaces, int nb_attributes, int nb_defaulted,
                               const xmlChar **attributes) {
	auto &s = StreamState(ctx);
	if (s.stopped) {
		return;
	}
	if (InEntity(ctx)) {
		xmlSAX2StartElementNs(ctx, localname, prefix, URI, nb_namespaces, namespaces, nb_attributes, nb_defaulted,
		                      attributes);
		return;
	}
	s.depth++;
	s.holds_record.push_back(false);
	if (s.record_depth == 0) {
		if (IsRecordStart(s, localname, prefix)) {
			s.record_depth = s.depth;
			s.record_start_bytes = s.fed_bytes;
			std::fill(s.holds_record.begin(), s.holds_record.end(), true);
		} else if (!s.root_element.empty() && s.root_depth == 0 &&
		           NameEquals(s.root_element, localname, prefix, s.keep_prefix)) {
			s.root_depth = s.depth;
		}
	}
	xmlSAX2StartElementNs(ctx, localname, prefix, URI, nb_namespaces, namespaces, nb_attributes, nb_defaulted,
	                      attributes);
}

static bool IsBlankText(xmlNodePtr node) {
	if (node->type != XML_TEXT_NODE || !node->content) {
		return false;
	}
	for (const xmlChar *c = node->content; *c; c++) {
		if (!std::isspace(*c)) {
			return false;
		}
	}
	return true;
}

// Frees indentation between child elements in a kept inference sample, which inference never reads.
// Mixed content keeps its whitespace.
static void DropFormattingText(xmlNodePtr node) {
	bool has_element = false;
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			has_element = true;
		} else if ((child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) && !IsBlankText(child)) {
			return;
		}
	}
	if (!has_element) {
		return;
	}
	xmlNodePtr child = node->children;
	while (child) {
		xmlNodePtr next = child->next;
		if (IsBlankText(child)) {
			xmlUnlinkNode(child);
			xmlFreeNode(child);
		}
		child = next;
	}
}

static void StreamEndElement(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI) {
	auto &s = StreamState(ctx);
	if (s.stopped) {
		return;
	}
	if (InEntity(ctx)) {
		xmlSAX2EndElementNs(ctx, localname, prefix, URI);
		return;
	}
	auto parser = static_cast<xmlParserCtxtPtr>(ctx);
	xmlNodePtr node = parser->node;
	xmlSAX2EndElementNs(ctx, localname, prefix, URI);
	if (s.keep_records && s.record_depth != 0 && node) {
		DropFormattingText(node);
	}
	bool holds_record = s.holds_record.back();
	s.holds_record.pop_back();

	if (s.record_depth == s.depth) {
		s.record_depth = 0;
		if (!s.keep_records) {
			xmlUnlinkNode(node);
		}
		XMLSchemaInference::ApplyXsiNil(node);
		s.records.push_back(node);
		s.records_completed++;
		if (s.max_records > 0 && s.records_completed >= s.max_records) {
			s.stopped = true;
			xmlStopParser(parser);
		}
	} else if (s.record_depth == 0 && node) {
		if (s.depth == s.root_depth) {
			s.root_done = true;
		}
		if (!holds_record) {
			xmlUnlinkNode(node);
			xmlFreeNode(node);
		} else if (!s.keep_records) {
			// Records unlinked from this element may still point at namespaces it declares.
			xmlUnlinkNode(node);
			s.garbage.push_back(node);
		}
	}
	s.depth--;
}

// Content outside a record is never read, so it is never built, except an entity's content.
static bool InRecord(void *ctx) {
	auto &s = StreamState(ctx);
	return !s.stopped && (s.record_depth > 0 || InEntity(ctx));
}

static void StreamCharacters(void *ctx, const xmlChar *ch, int len) {
	if (InRecord(ctx)) {
		xmlSAX2Characters(ctx, ch, len);
	}
}

static void StreamCdataBlock(void *ctx, const xmlChar *value, int len) {
	if (InRecord(ctx)) {
		xmlSAX2CDataBlock(ctx, value, len);
	}
}

static void StreamComment(void *ctx, const xmlChar *value) {
	if (InRecord(ctx)) {
		xmlSAX2Comment(ctx, value);
	}
}

static void StreamProcessingInstruction(void *ctx, const xmlChar *target, const xmlChar *data) {
	if (InRecord(ctx)) {
		xmlSAX2ProcessingInstruction(ctx, target, data);
	}
}

static void StreamReference(void *ctx, const xmlChar *name) {
	if (InRecord(ctx)) {
		xmlSAX2Reference(ctx, name);
	}
}

XMLRecordStream::XMLRecordStream(std::unique_ptr<FileHandle> file_p, const std::string &filename_p,
                                 const XMLSchemaOptions &options, idx_t max_records, bool keep_records,
                                 std::string prefix_p)
    : filename(filename_p), file(std::move(file_p)), buffer(CHUNK_SIZE), prefix(std::move(prefix_p)) {
	std::string tag = options.record_element;
	if (tag.size() >= 2 && tag[0] == '/' && tag[1] == '/') {
		tag = tag.substr(2);
	}
	if (tag.find_first_of("/@*[]") != std::string::npos) {
		throw InvalidInputException("SAX reader: record_element '%s' contains XPath syntax not supported in SAX mode. "
		                            "Use a simple element name (e.g., 'item') or set streaming:=false.",
		                            options.record_element);
	}
	state.record_tag = tag;
	state.root_element = options.root_element;
	state.root_is_record = options.max_depth == 0;
	state.keep_prefix = options.namespaces == "keep";
	state.keep_records = keep_records;
	state.max_records = max_records;
	state.max_record_bytes = options.max_record_bytes;

	memset(&handler, 0, sizeof(handler));
	xmlSAXVersion(&handler, 2);
	handler.startElementNs = StreamStartElement;
	handler.endElementNs = StreamEndElement;
	handler.characters = StreamCharacters;
	handler.ignorableWhitespace = StreamCharacters;
	handler.cdataBlock = StreamCdataBlock;
	handler.comment = StreamComment;
	handler.processingInstruction = StreamProcessingInstruction;
	handler.reference = StreamReference;

	XMLUtils::EnsureSecureParsing();
	ctxt = xmlCreatePushParserCtxt(&handler, nullptr, nullptr, 0, filename.c_str());
	if (!ctxt) {
		throw IOException("Could not create SAX push parser context for '%s'", filename);
	}
	ctxt->_private = &state;
	// XML_PARSE_HUGE lifts libxml2's default limits on text-node size and depth, which a large
	// streamed file would exceed.
	xmlCtxtUseOptions(ctxt, XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET | XML_PARSE_HUGE);
}

XMLRecordStream::~XMLRecordStream() {
	ReleaseRecords();
	if (ctxt) {
		if (ctxt->myDoc) {
			xmlFreeDoc(ctxt->myDoc);
			ctxt->myDoc = nullptr;
		}
		xmlFreeParserCtxt(ctxt);
	}
}

bool XMLRecordStream::ParseChunk() {
	if (finished) {
		return false;
	}
	const char *data = buffer.data();
	idx_t bytes_read;
	if (prefix_offset < prefix.size()) {
		data = prefix.data() + prefix_offset;
		bytes_read = MinValue<idx_t>(prefix.size() - prefix_offset, CHUNK_SIZE);
		prefix_offset += bytes_read;
	} else {
		bytes_read = NumericCast<idx_t>(file->Read(buffer.data(), CHUNK_SIZE));
	}
	bool terminate = bytes_read == 0;
	int result = xmlParseChunk(ctxt, terminate ? nullptr : data, static_cast<int>(bytes_read), terminate);
	state.fed_bytes += bytes_read;
	if (!prefix.empty() && prefix_offset == prefix.size()) {
		std::string().swap(prefix);
		prefix_offset = 0;
	}
	if (state.record_depth != 0 && state.max_record_bytes > 0 &&
	    state.fed_bytes - state.record_start_bytes > state.max_record_bytes) {
		throw OutOfMemoryException("A record in '%s' spans more than %s of input, a quarter of memory_limit. Raise "
		                           "memory_limit to read it.",
		                           filename, StringUtil::BytesToHumanReadableString(state.max_record_bytes));
	}
	// Reaching max_records stops the parse mid-document, which libxml2 reports as an error.
	if (state.stopped) {
		finished = true;
		return false;
	}
	if (result != 0) {
		throw IOException("SAX parsing error in file '%s': %s", filename, DescribeParseError(ctxt));
	}
	finished = terminate;
	return !finished;
}

void XMLRecordStream::ReleaseRecords() {
	if (!state.keep_records) {
		for (auto node : state.records) {
			xmlFreeNode(node);
		}
	}
	state.records.clear();
	for (auto node : state.garbage) {
		xmlFreeNode(node);
	}
	state.garbage.clear();
}

xmlDocPtr XMLRecordStream::TakeDocument() {
	xmlDocPtr doc = ctxt->myDoc;
	ctxt->myDoc = nullptr;
	return doc;
}

std::string XMLRecordStream::DescribeParseError(xmlParserCtxtPtr ctx) {
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

std::vector<XMLColumnInfo> SAXStreamReader::InferSchemaFromStream(FileSystem &fs, const std::string &filename,
                                                                  const XMLSchemaOptions &options) {
	idx_t sample_count = static_cast<idx_t>(options.sample_size > 0 ? options.sample_size : 50);
	XMLDocRAII doc;
	{
		XMLRecordStream stream(OpenXMLFile(fs, filename, options.compression), filename, options, sample_count, true);
		while (stream.ParseChunk()) {
		}
		if (stream.Records().empty()) {
			return {};
		}
		doc = XMLDocRAII(stream.TakeDocument());
	}

	auto columns = XMLSchemaInference::InferSchemaFromDocument(doc, options);

	if (options.sample_size <= 0) {
		WidenSchemaForOutOfSampleValues(fs, filename, options, columns);
	}
	return columns;
}

static bool IsNullString(const std::string &text, const XMLSchemaOptions &options) {
	for (const auto &ns : options.null_strings) {
		if (text == ns) {
			return true;
		}
	}
	return false;
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

static bool IsStructuredField(xmlNodePtr field, const XMLSchemaOptions &options) {
	if (field->properties && options.attr_mode != "discard") {
		return true;
	}
	for (xmlNodePtr child = field->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			return true;
		}
	}
	return false;
}

static std::string FieldText(xmlNodePtr field, const XMLSchemaOptions &options) {
	std::string text;
	xmlChar *content = xmlNodeGetContent(field);
	if (content) {
		text =
		    XMLSchemaInference::CleanTextContent(reinterpret_cast<const char *>(content), options.preserve_whitespace);
		xmlFree(content);
	}
	return text;
}

void SAXStreamReader::WidenSchemaForOutOfSampleValues(FileSystem &fs, const std::string &filename,
                                                      const XMLSchemaOptions &options,
                                                      std::vector<XMLColumnInfo> &columns) {
	std::vector<bool> widen(columns.size(), false);

	// A child element first seen past the sample window is missing from the schema, so querying it
	// would be a bind error. This pass already streams every record, so it also discovers those
	// fields and appends them. Late attributes are out of scope: their naming depends on attr_mode.
	std::unordered_set<std::string> known;
	known.reserve(columns.size() * 2);
	std::unordered_map<std::string, idx_t> element_columns;
	for (idx_t i = 0; i < columns.size(); i++) {
		known.insert(columns[i].name);
		if (!columns[i].is_attribute) {
			element_columns.emplace(columns[i].name, i);
		}
	}
	std::vector<std::string> discovered;

	// ignore_errors so ConvertToValuePublic returns a typed NULL on an uncastable value instead of
	// throwing; combined with the empty/nullstr pre-filter below, a NULL result means "cast failed".
	XMLSchemaOptions verify_opts = options;
	verify_opts.ignore_errors = true;

	auto test_field = [&](xmlNodePtr field, idx_t i) {
		auto &type = columns[i].type;
		bool unknown = IsUnknownType(type);
		if (!unknown && !IsWidenableScalar(type)) {
			return;
		}
		if (IsStructuredField(field, options)) {
			widen[i] = unknown;
			return;
		}
		auto text = FieldText(field, options);
		if (text.empty() || IsNullString(text, options)) {
			return;
		}
		if (unknown) {
			widen[i] = true;
			return;
		}
		Value v = XMLSchemaInference::ConvertToValuePublic(text, type, verify_opts, columns[i].winning_datetime_format);
		widen[i] = v.IsNull();
	};

	XMLRecordStream stream(OpenXMLFile(fs, filename, options.compression), filename, options);
	bool more;
	do {
		more = stream.ParseChunk();
		for (auto record : stream.Records()) {
			for (xmlNodePtr field = record->children; field; field = field->next) {
				if (field->type != XML_ELEMENT_NODE) {
					continue;
				}
				std::string name(reinterpret_cast<const char *>(field->name));
				if (known.insert(name).second) {
					discovered.push_back(name);
					continue;
				}
				auto it = element_columns.find(name);
				if (it != element_columns.end() && !widen[it->second]) {
					test_field(field, it->second);
				}
			}
		}
		stream.ReleaseRecords();
	} while (more);

	for (idx_t i = 0; i < columns.size(); i++) {
		if (widen[i]) {
			columns[i].type =
			    IsUnknownType(columns[i].type) ? WidenUnknownToVarchar(columns[i].type) : LogicalType::VARCHAR;
			columns[i].winning_datetime_format.clear();
		}
	}

	// A column first seen past the sample has no inferred type to widen from, so it is VARCHAR.
	// `columns :=` overrides it.
	for (const auto &name : discovered) {
		columns.emplace_back(name, LogicalType::VARCHAR);
	}
}

} // namespace duckdb
