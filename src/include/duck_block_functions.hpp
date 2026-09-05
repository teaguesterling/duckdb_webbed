#pragma once

#include "duckdb.hpp"

namespace duckdb {

struct CaptureSpec; // defined in duck_block_functions.cpp; see capture_attributes


/**
 * DuckBlockFunctions provides scalar functions for converting between HTML/XML and duck_blocks.
 *
 * Functions:
 * - html_to_duck_blocks(html HTML) -> LIST(duck_block)
 *   Parses HTML and returns a list of duck_block structs representing block-level elements.
 *
 * - duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
 *   Serializes a list of duck_block structs back to HTML.
 */
class DuckBlockFunctions {
public:
	static void Register(ExtensionLoader &loader);

	// Core parsing helper
	static vector<Value> HtmlToDuckBlocks(const std::string &html_str, const CaptureSpec &spec);

private:
	// html_to_duck_blocks(html HTML) -> LIST(duck_block)
	static void HtmlToDuckBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
	static void DuckBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// read_html_blocks(path VARCHAR / LIST(VARCHAR) [, filename=false, ignore_errors=false, maximum_file_size=...])
	static unique_ptr<FunctionData> ReadHTMLBlocksBind(ClientContext &context, TableFunctionBindInput &input,
	                                                   vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ReadHTMLBlocksInit(ClientContext &context,
	                                                               TableFunctionInitInput &input);
	static unique_ptr<LocalTableFunctionState> ReadHTMLBlocksInitLocal(ExecutionContext &context,
	                                                                   TableFunctionInitInput &input,
	                                                                   GlobalTableFunctionState *global_state);
	static OperatorPartitionData ReadHTMLBlocksGetPartitionData(ClientContext &context,
	                                                            TableFunctionGetPartitionInput &input);
	static void ReadHTMLBlocksFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);

	// parse_html_blocks(html VARCHAR / HTML / LIST(VARCHAR) / LIST(HTML) [, ignore_errors=false])
	static unique_ptr<FunctionData> ParseHTMLBlocksBind(ClientContext &context, TableFunctionBindInput &input,
	                                                    vector<LogicalType> &return_types, vector<string> &names);
	static unique_ptr<GlobalTableFunctionState> ParseHTMLBlocksInit(ClientContext &context,
	                                                                TableFunctionInitInput &input);
	static unique_ptr<LocalTableFunctionState> ParseHTMLBlocksInitLocal(ExecutionContext &context,
	                                                                    TableFunctionInitInput &input,
	                                                                    GlobalTableFunctionState *global_state);
	static void ParseHTMLBlocksFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
};

} // namespace duckdb
