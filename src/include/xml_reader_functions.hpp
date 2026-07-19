#pragma once

#include "duckdb.hpp"
#include "xml_sax_reader.hpp"
#include "xml_schema_inference.hpp"

#include <mutex>

namespace duckdb {

// Parse mode for document reading
enum class ParseMode {
	XML, // Strict XML parsing
	HTML // Lenient HTML parsing
};

class XMLReaderFunctions {
public:
	static void Register(ExtensionLoader &loader);

	// Replacement scan function for direct file querying
	static unique_ptr<TableRef> ReadXMLReplacement(ClientContext &context, ReplacementScanInput &input,
	                                               optional_ptr<ReplacementScanData> data);

private:
	// Internal unified functions (used by both XML and HTML)
	static unique_ptr<FunctionData> ReadDocumentObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                        vector<LogicalType> &return_types, vector<string> &names,
	                                                        ParseMode mode);
	static unique_ptr<FunctionData> ReadDocumentBind(ClientContext &context, TableFunctionBindInput &input,
	                                                 vector<LogicalType> &return_types, vector<string> &names,
	                                                 ParseMode mode);
	static void ReadDocumentObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static void ReadDocumentFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<GlobalTableFunctionState> ReadDocumentInit(ClientContext &context, TableFunctionInitInput &input);
	// Per-thread state + batch-index tagging for order-preserving multi-file parallelism (issue #72).
	// Shared by all four read_xml/read_html(/_objects) table functions.
	static unique_ptr<LocalTableFunctionState> ReadDocumentInitLocal(ExecutionContext &context,
	                                                                 TableFunctionInitInput &input,
	                                                                 GlobalTableFunctionState *global_state);
	static OperatorPartitionData ReadDocumentGetPartitionData(ClientContext &context,
	                                                          TableFunctionGetPartitionInput &input);

	// Public XML functions (delegate to internal functions)
	static void ReadXMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                   vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLObjectsInit(ClientContext &context,
	                                                               TableFunctionInitInput &input);

	static void ReadXMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                            vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLInit(ClientContext &context, TableFunctionInitInput &input);

	// Public HTML functions (delegate to internal functions)
	static void ReadHTMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                    vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadHTMLObjectsInit(ClientContext &context,
	                                                                TableFunctionInitInput &input);

	static void ReadHTMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadHTMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                             vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadHTMLInit(ClientContext &context, TableFunctionInitInput &input);

	// HTML table extraction functions
	static void HTMLExtractTablesFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> HTMLExtractTablesBind(ClientContext &context, TableFunctionBindInput &input,
	                                                      vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> HTMLExtractTablesInit(ClientContext &context,
	                                                                  TableFunctionInitInput &input);

	// parse_xml_objects / parse_html_objects - parse XML/HTML strings and return raw content
	static void ParseDocumentObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ParseDocumentObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                         vector<LogicalType> &return_types, vector<string> &names,
	                                                         ParseMode mode);
	static unique_ptr<GlobalTableFunctionState> ParseDocumentObjectsInit(ClientContext &context,
	                                                                     TableFunctionInitInput &input);

	// parse_xml / parse_html - parse XML/HTML strings with schema inference
	static void ParseDocumentFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ParseDocumentBind(ClientContext &context, TableFunctionBindInput &input,
	                                                  vector<LogicalType> &return_types, vector<string> &names,
	                                                  ParseMode mode);
	static unique_ptr<GlobalTableFunctionState> ParseDocumentInit(ClientContext &context,
	                                                              TableFunctionInitInput &input);

	// Public parse_xml functions (delegate to internal functions)
	static unique_ptr<FunctionData> ParseXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                    vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<FunctionData> ParseXMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                             vector<LogicalType> &return_types, vector<string> &names);

	// Public parse_html functions (delegate to internal functions)
	static unique_ptr<FunctionData> ParseHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                     vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<FunctionData> ParseHTMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                              vector<LogicalType> &return_types, vector<string> &names);
};

// Function data structures
struct XMLReadFunctionData : public TableFunctionData {
	vector<string> files;
	bool ignore_errors = false;
	idx_t max_file_size = 16777216;        // 16MB default (files above this use SAX streaming)
	ParseMode parse_mode = ParseMode::XML; // Parsing mode (XML or HTML)

	// For _objects functions
	bool include_filename = false;

	// Explicit schema information (when columns parameter is provided)
	bool has_explicit_schema = false;
	vector<string> column_names;
	vector<LogicalType> column_types;
	vector<string> column_datetime_formats; // Per-column winning format from inference

	// Schema inference options (for read_xml with auto schema detection)
	XMLSchemaOptions schema_options;

	// Full inferred schema with is_attribute flags — currently populated but used only
	// by the ExtractSingleRecord path (Phase 2 SAX mode will need this since the
	// union-schema code path sets has_explicit_schema=true for DOM extraction)
	std::vector<XMLColumnInfo> inferred_schema;

	// Union by name - combine files with different schemas
	bool union_by_name = false;
};

// Shared, read-only-after-init state plus a mutex-guarded file dispatcher. All per-file
// cursor state lives in XMLReadLocalState (below), so each worker thread can process a
// different file concurrently. See issue #72.
struct XMLReadGlobalState : public GlobalTableFunctionState {
	vector<string> files;

	idx_t MaxThreads() const override {
		// One worker per file; a single-file read stays single-threaded (behaviour unchanged,
		// including the 2-4 GiB whole-document path). DuckDB caps this to the available threads.
		return files.empty() ? 1 : files.size();
	}

	// Hand the next file index to a worker, or INVALID_INDEX once the work list is exhausted.
	idx_t ClaimNextFile() {
		std::lock_guard<std::mutex> guard(file_lock);
		if (next_file_index >= files.size()) {
			return DConstants::INVALID_INDEX;
		}
		return next_file_index++;
	}

private:
	std::mutex file_lock;
	idx_t next_file_index = 0;
};

// Per-worker cursor over one file at a time. A worker claims a file from the global
// dispatcher, emits at most ONE file's rows per output chunk (partial chunks at file
// boundaries are expected), and tags each chunk with a batch index so DuckDB's
// order-preserving reassembly restores glob/file order under parallel execution.
struct XMLReadLocalState : public LocalTableFunctionState {
	// Batch index layout: high bits = file index (glob order), low bits = chunk-within-file.
	// FILE_SHIFT gives 2^32 chunks/file and 2^32 files — both far beyond any real input
	// (2^32 chunks x 2048 rows would need ~10^13 rows in a <=4 GiB file). Keeps batch indices
	// strictly ordered by (file_index, chunk), i.e. exactly glob/file order.
	static constexpr idx_t FILE_SHIFT = 32;

	idx_t file_index = DConstants::INVALID_INDEX; // file this worker is processing / last processed
	string current_filename;                      // name of that file (per-file value source, never the global cursor)
	idx_t chunk_counter = 0;                      // within-file index for the NEXT produced chunk
	idx_t last_batch_index = 0; // batch index of the most recent chunk (get_partition_data returns this)
	bool have_file = false;     // a file is claimed and being processed
	bool file_loaded = false;   // per-file resources are initialized

	// Lazy DOM iteration state
	XMLDocRAII current_doc;                  // DOM stays alive across scan calls
	std::vector<xmlNodePtr> record_elements; // Pointers into DOM; valid only while current_doc is alive
	idx_t current_record_index = 0;          // Position in record_elements
	int remaining_depth = 0;                 // For extraction depth calculation

	// SAX streaming state (used when streaming=true and file exceeds maximum_file_size)
	bool use_sax = false;
	std::unique_ptr<SAXRecordAccumulator> sax_accumulator; // Accumulator state (persists across scan calls)
	std::unique_ptr<SAXCallbackContext> sax_ctx;           // Callback context (persists across scan calls)
	xmlParserCtxtPtr sax_parser_ctx = nullptr;             // Push parser context (persists across scan calls)
	std::unique_ptr<FileHandle> sax_file_handle;           // File handle (persists across scan calls)
	xmlSAXHandler sax_handler;                             // SAX handler (must outlive parser context)
	std::vector<SAXRecordAccumulator> sax_pending_records; // Records completed during current chunk

	~XMLReadLocalState() {
		if (sax_parser_ctx) {
			xmlFreeParserCtxt(sax_parser_ctx);
			sax_parser_ctx = nullptr;
		}
	}

	// Release all per-file resources (when a file is finished or skipped). Keeps file_index /
	// last_batch_index so a just-produced chunk can still be tagged by get_partition_data.
	void ResetFileResources() {
		current_doc = XMLDocRAII();
		record_elements.clear();
		current_record_index = 0;
		if (sax_parser_ctx) {
			xmlFreeParserCtxt(sax_parser_ctx);
			sax_parser_ctx = nullptr;
		}
		sax_accumulator.reset();
		sax_ctx.reset();
		sax_file_handle.reset();
		sax_pending_records.clear();
		use_sax = false;
		file_loaded = false;
	}
};

// HTML table extraction function data
struct HTMLTableExtractionData : public TableFunctionData {
	string html_content;
};

// String-based XML/HTML parsing function data (for parse_xml, parse_html, etc.)
struct XMLParseData : public TableFunctionData {
	string xml_content; // Input XML/HTML string
	bool ignore_errors = false;
	ParseMode parse_mode = ParseMode::XML;

	// Schema information (for parse_xml/parse_html with schema inference)
	bool has_explicit_schema = false;
	vector<string> column_names;
	vector<LogicalType> column_types;
	vector<string> column_datetime_formats; // Per-column winning format from inference
	XMLSchemaOptions schema_options;
};

struct XMLParseGlobalState : public GlobalTableFunctionState {
	idx_t current_row = 0;
	std::vector<std::vector<Value>> extracted_rows;

	idx_t MaxThreads() const override {
		return 1;
	}
};

struct HTMLTableExtractionGlobalState : public GlobalTableFunctionState {
	vector<vector<vector<string>>> all_tables; // [table][row][column]
	idx_t current_table = 0;
	idx_t current_row = 0;

	idx_t MaxThreads() const override {
		return 1; // Single-threaded for now
	}
};

} // namespace duckdb
