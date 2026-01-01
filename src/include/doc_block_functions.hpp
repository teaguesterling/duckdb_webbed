#pragma once

#include "duckdb.hpp"

namespace duckdb {

/**
 * DocBlockFunctions provides scalar functions for converting between HTML/XML and doc_block.
 *
 * Functions:
 * - html_to_doc_blocks(html HTML) -> LIST(doc_block)
 *   Parses HTML and returns a list of doc_block structs representing block-level elements.
 *
 * - doc_blocks_to_html(blocks LIST(doc_block)) -> HTML
 *   Serializes a list of doc_block structs back to HTML.
 */
class DocBlockFunctions {
public:
	static void Register(ExtensionLoader &loader);

private:
	// html_to_doc_blocks(html HTML) -> LIST(doc_block)
	static void HtmlToDocBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_blocks_to_html(blocks LIST(doc_block)) -> HTML
	static void DocBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
