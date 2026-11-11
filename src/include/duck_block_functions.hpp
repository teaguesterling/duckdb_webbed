#pragma once

#include "duckdb.hpp"
#include "duckdb_compat.hpp"
#include "xml_utils.hpp"

namespace duckdb {

// The deepest duck_block container level read_html_blocks can emit. <html> and <body> take two of libxml2's levels,
// and a body child is level 1. duck_blocks_to_html refuses a deeper container, so a reader's output round trips. A
// block it renders with optional wrappers, <code> in <pre> and a table's <thead> and <tbody>, drops them near this
// level.
static constexpr int32_t MAX_DUCK_BLOCK_CONTAINER_LEVEL = LIBXML_MAX_TREE_DEPTH - 2;

struct CaptureSpec; // defined in duck_block_functions.cpp; see capture_attributes

// Receives each duck_block as the HTML walk produces it, in document order. A reader writes blocks into buffer-managed
// rows as they arrive instead of holding the whole document's blocks as Values.
class DuckBlockSink {
public:
	virtual ~DuckBlockSink() = default;
	// `block` is a duck_block STRUCT of the seven canonical fields.
	virtual void Append(Value block) = 0;
};

// Collects blocks into a vector, for html_to_duck_blocks's LIST result.
class DuckBlockListSink : public DuckBlockSink {
public:
	vector<Value> blocks;
	void Append(Value block) override {
		blocks.push_back(std::move(block));
	}
};

/**
 * WebbedDuckBlockFunctions provides scalar functions for converting between HTML/XML and duck_blocks.
 *
 * Functions:
 * - html_to_duck_blocks(html HTML) -> LIST(duck_block)
 *   Parses HTML and returns a list of duck_block structs representing block-level elements.
 *
 * - duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
 *   Serializes a list of duck_block structs back to HTML.
 */
class WebbedDuckBlockFunctions {
public:
	static void Register(ExtensionLoader &loader);

	// Parse HTML and write its blocks to `sink`.
	static void HtmlToDuckBlocks(const std::string &html_str, const CaptureSpec &spec, DuckBlockSink &sink,
	                             HTMLInputEncoding input = HTMLInputEncoding::DECODED_TEXT);
	// Write a parsed document's blocks to `sink`. The walk caches per-node results in each node's _private slot, so
	// the document must not be shared with another reader.
	static void WalkHTMLDocument(xmlDocPtr doc, const CaptureSpec &spec, DuckBlockSink &sink);

private:
	// html_to_duck_blocks(html HTML) -> LIST(duck_block)
	static void HtmlToDuckBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
	static void DuckBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// read_html_blocks(path VARCHAR / LIST(VARCHAR) [, filename=false, ignore_errors=false, maximum_file_size=...])
	static unique_ptr<FunctionData> ReadHTMLBlocksBind(ClientContext &context, TableFunctionBindInput &input,
	                                                   vector<LogicalType> &return_types, vector<CompatName> &names);
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
	                                                    vector<LogicalType> &return_types, vector<CompatName> &names);
	static unique_ptr<GlobalTableFunctionState> ParseHTMLBlocksInit(ClientContext &context,
	                                                                TableFunctionInitInput &input);
	static unique_ptr<LocalTableFunctionState> ParseHTMLBlocksInitLocal(ExecutionContext &context,
	                                                                    TableFunctionInitInput &input,
	                                                                    GlobalTableFunctionState *global_state);
	static void ParseHTMLBlocksFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output);
};

} // namespace duckdb
