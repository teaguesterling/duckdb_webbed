#pragma once

#include "duckdb.hpp"
#include "xml_schema_inference.hpp"

namespace duckdb {

// Parse mode for document reading
enum class ParseMode {
	XML,  // Strict XML parsing
	HTML  // Lenient HTML parsing
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
	static unique_ptr<GlobalTableFunctionState> ReadDocumentInit(ClientContext &context,
	                                                              TableFunctionInitInput &input);

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
};

// Function data structures
struct XMLReadFunctionData : public TableFunctionData {
	vector<string> files;
	bool ignore_errors = false;
	idx_t max_file_size = 16777216; // 16MB default
	ParseMode parse_mode = ParseMode::XML; // Parsing mode (XML or HTML)

	// For _objects functions
	bool include_filename = false;

	// Explicit schema information (when columns parameter is provided)
	bool has_explicit_schema = false;
	vector<string> column_names;
	vector<LogicalType> column_types;

	// Schema inference options (for read_xml with auto schema detection)
	XMLSchemaOptions schema_options;
};

struct XMLReadGlobalState : public GlobalTableFunctionState {
	idx_t file_index = 0;
	vector<string> files;

	idx_t MaxThreads() const override {
		return 1; // Single-threaded for now
	}
};

// HTML table extraction function data
struct HTMLTableExtractionData : public TableFunctionData {
	string html_content;
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
