#pragma once

#include "duckdb.hpp"
#include "xml_schema_inference.hpp"

namespace duckdb {

class XMLReaderFunctions {
public:
	static void Register(ExtensionLoader &loader);

	// Replacement scan function for direct file querying
	static unique_ptr<TableRef> ReadXMLReplacement(ClientContext &context, ReplacementScanInput &input,
	                                               optional_ptr<ReplacementScanData> data);

private:
	// Table functions for reading XML files
	static void ReadXMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                   vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLObjectsInit(ClientContext &context,
	                                                               TableFunctionInitInput &input);

	// Simplified read_xml function (without schema inference for now)
	static void ReadXMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                            vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLInit(ClientContext &context, TableFunctionInitInput &input);

	// HTML reading functions
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
