#pragma once

#include "duckdb.hpp"

namespace duckdb {

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

private:
	// html_to_duck_blocks(html HTML) -> LIST(duck_block)
	static void HtmlToDuckBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
	static void DuckBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
