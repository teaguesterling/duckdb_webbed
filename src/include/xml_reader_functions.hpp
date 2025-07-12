#pragma once

#include "duckdb.hpp"

namespace duckdb {

class XMLReaderFunctions {
public:
	static void Register(DatabaseInstance &db);
	
private:
	// Table functions for reading XML files
	static void ReadXMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
	                                                     vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLObjectsInit(ClientContext &context, TableFunctionInitInput &input);
	
	// Simplified read_xml function (without schema inference for now)
	static void ReadXMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
	static unique_ptr<FunctionData> ReadXMLBind(ClientContext &context, TableFunctionBindInput &input,
	                                             vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadXMLInit(ClientContext &context, TableFunctionInitInput &input);
};

// Function data structures
struct XMLReadFunctionData : public TableFunctionData {
	vector<string> files;
	bool ignore_errors = false;
	idx_t max_file_size = 16777216; // 16MB default
};

struct XMLReadGlobalState : public GlobalTableFunctionState {
	idx_t file_index = 0;
	vector<string> files;
	
	idx_t MaxThreads() const override {
		return 1; // Single-threaded for now
	}
};

} // namespace duckdb