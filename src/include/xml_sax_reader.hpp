#pragma once

#include "duckdb.hpp"
#include "xml_schema_inference.hpp"
#include <libxml/parser.h>
#include <libxml/xmlstring.h>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace duckdb {

// State machine for accumulating one XML record from SAX events
enum class SAXAccumulatorState {
	SEEKING_RECORD, // Waiting for the start of a record element
	IN_RECORD,      // Inside a record element, accumulating data
	RECORD_COMPLETE // A complete record has been accumulated
};

// A single occurrence of a record field, tagged by payload kind. Occurrences are stored in
// document order so a field mixing bare-text and nested-XML siblings keeps its original sequence.
struct FieldOccurrence {
	bool is_xml;          // true = nested-XML fragment, false = text payload
	std::string payload;  // serialized XML fragment (is_xml) or cleaned text (!is_xml)
	std::string own_attrs; // the field element's OWN attributes, pre-serialized and escaped as
	                        // ` name="value"...` (empty if none), spliced into the reconstructed
	                        // open tag so a direct child like <CustomFunctionReference id=".."/>
	                        // keeps its attributes under streaming (DOM parity).
};

// Accumulates field data from SAX events for a single XML record
struct SAXRecordAccumulator {
	// Current state
	SAXAccumulatorState state = SAXAccumulatorState::SEEKING_RECORD;

	// Configuration
	std::string record_tag;     // Tag name to match as record element (empty = auto-detect at depth 2)
	std::string namespace_mode; // "strip" or "keep"
	int record_depth = 0;       // Depth at which records appear (0 = not yet determined)

	// Element tracking
	std::vector<std::string> element_stack; // Stack of element names
	int current_depth = 0;                  // Current nesting depth

	// Accumulated data for current record. Each field maps to its occurrences in document order,
	// each tagged as text or nested-XML so the rich-typing path (STRUCT, LIST<STRUCT>) can re-parse
	// the inner XML while preserving the original sequence of mixed text/XML siblings.
	std::unordered_map<std::string, std::vector<FieldOccurrence>> current_fields; // field_name -> ordered occurrences
	std::unordered_map<std::string, std::string> current_attributes;              // attr_name -> value

	// First-seen insertion order of current_fields / current_attributes keys, so synthetic XML
	// built for schema inference reflects document order (deterministic column order).
	std::vector<std::string> field_order;
	std::vector<std::string> attribute_order;

	// Namespace declarations (prefix -> URI) seen anywhere in the document, accumulated globally so
	// reparsed nested-XML fragments can resolve prefixed names. Intentionally NOT cleared by Reset()
	// since root-level xmlns: declarations appear before the first record.
	std::map<std::string, std::string> namespace_declarations;

	// Text accumulation (SAX may split text across multiple characters() callbacks)
	std::string current_text;
	std::string current_element_name; // Name of the element whose text we're accumulating

	// Own attributes of the direct child of the record currently being accumulated (relative_depth==1),
	// serialized as ` name="value"...` (escaped). Held from the child's start until its close, then
	// stored on the FieldOccurrence so reconstruction can splice them into the field's open tag.
	std::string current_field_attrs;

	// Nested XML accumulation (for elements deeper than record_depth+1)
	std::string nested_xml;
	int nested_depth = 0; // How deep we are in nested elements (0 = direct child of record)

	// Output signal
	bool row_ready = false;

	// Reset accumulator for next record
	void Reset();

	// Get a field's occurrences in document order (empty vector if the field is absent)
	const std::vector<FieldOccurrence> &GetOccurrences(const std::string &name) const;

	// Serialize accumulated namespace declarations as ` xmlns:prefix="uri"...` (empty if none),
	// ready to splice into a synthetic wrapper element's open tag.
	std::string BuildNamespaceDeclarations() const;

	// Check if an attribute exists
	bool HasAttribute(const std::string &name) const;

	// Get an attribute value (empty string if not found)
	std::string GetAttribute(const std::string &name) const;
};

// SAX2 callback context passed as void* ctx to libxml2 SAX handlers
struct SAXCallbackContext {
	SAXRecordAccumulator *accumulator = nullptr;
	idx_t max_rows = 0;                                             // Maximum records to accumulate (0 = unlimited)
	idx_t rows_completed = 0;                                       // Number of completed records so far
	bool stop_parsing = false;                                      // Signal to stop the push parser
	std::vector<SAXRecordAccumulator> *completed_records = nullptr; // Where to store completed records
	bool preserve_whitespace = true;
	bool discard_attrs = false; // precomputed from XMLSchemaOptions::attr_mode == "discard" (hot-path flag)
};

// SAX2 callback functions (static, matching libxml2 signatures)
void SAXStartElementNs(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI,
                       int nb_namespaces, const xmlChar **namespaces, int nb_attributes, int nb_defaulted,
                       const xmlChar **attributes);

void SAXEndElementNs(void *ctx, const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI);

void SAXCharacters(void *ctx, const xmlChar *ch, int len);

void SAXCdataBlock(void *ctx, const xmlChar *ch, int len);

// Main SAX streaming reader class
class SAXStreamReader {
public:
	// Create a SAX handler struct with our callbacks
	static xmlSAXHandler CreateSAXHandler();

	// Read records from file using SAX push parsing via DuckDB FileSystem.
	// Returns accumulated records as vectors of (field_name -> value) maps.
	// Reads file in 64KB chunks.
	static std::vector<SAXRecordAccumulator> ReadRecords(FileSystem &fs, const std::string &filename,
	                                                     const XMLSchemaOptions &options, idx_t max_rows = 0);

	// SAX-based schema inference: accumulate first N records,
	// build synthetic XML, feed to existing InferSchema.
	static std::vector<XMLColumnInfo> InferSchemaFromStream(FileSystem &fs, const std::string &filename,
	                                                        const XMLSchemaOptions &options);

	// Convert accumulated record data to a row of Values
	static std::vector<Value> AccumulatorToRow(const SAXRecordAccumulator &accumulator,
	                                           const std::vector<std::string> &column_names,
	                                           const std::vector<LogicalType> &column_types,
	                                           const XMLSchemaOptions &options,
	                                           const std::vector<std::string> &column_datetime_formats = {},
	                                           const std::vector<XMLColumnInfo> &inferred_schema = {});

private:
	static constexpr idx_t SAX_CHUNK_SIZE = 65536; // 64KB read chunks
};

} // namespace duckdb
