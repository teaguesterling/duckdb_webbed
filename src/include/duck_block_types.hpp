#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"

namespace duckdb {

/**
 * DuckBlockTypes provides type definitions and utilities for working with doc_element structures.
 *
 * This is a header-only interface that mirrors the duck_block_utils extension's type definitions,
 * enabling webbed to produce doc_element output without a compile-time dependency on duck_block_utils.
 *
 * The doc_element type represents a document element with the following structure:
 * STRUCT(
 *     kind VARCHAR,            -- 'block' or 'inline'
 *     element_type VARCHAR,    -- 'heading', 'paragraph', 'code', etc.
 *     content VARCHAR,         -- The element's text content
 *     level INTEGER,           -- Hierarchy level (NULL if not applicable)
 *     encoding VARCHAR,        -- 'text', 'json', 'yaml', 'html', 'xml'
 *     attributes MAP(VARCHAR, VARCHAR),  -- Key-value metadata
 *     element_order INTEGER    -- Position in document (0-indexed)
 * )
 *
 * For headings, the heading level (1-6) is stored in attributes['heading_level'],
 * not in the 'level' field. The 'level' field is reserved for hierarchy depth.
 */
class DuckBlockTypes {
public:
	// Create the doc_element type (unified type for both blocks and inlines)
	static LogicalType DuckBlockType() {
		child_list_t<LogicalType> struct_children;
		struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
		struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
		struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
		struct_children.push_back(make_pair("level", LogicalType::INTEGER));
		struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
		struct_children.push_back(
		    make_pair("attributes", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
		struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));

		return LogicalType::STRUCT(std::move(struct_children));
	}

	// Alias for semantic clarity
	static LogicalType DocElementType() {
		return DuckBlockType();
	}

	// Create a LIST(doc_element) type
	static LogicalType DuckBlockListType() {
		return LogicalType::LIST(DuckBlockType());
	}

	// Alias for semantic clarity
	static LogicalType DocElementListType() {
		return DuckBlockListType();
	}

	// Field indices for doc_element struct
	static constexpr idx_t KIND_IDX = 0;
	static constexpr idx_t ELEMENT_TYPE_IDX = 1;
	static constexpr idx_t CONTENT_IDX = 2;
	static constexpr idx_t LEVEL_IDX = 3;
	static constexpr idx_t ENCODING_IDX = 4;
	static constexpr idx_t ATTRIBUTES_IDX = 5;
	static constexpr idx_t ELEMENT_ORDER_IDX = 6;

	// Kind values
	static constexpr const char *KIND_BLOCK = "block";
	static constexpr const char *KIND_INLINE = "inline";
	static constexpr const char *KIND_VALUE = "value";

	// Core block type names
	static constexpr const char *TYPE_HEADING = "heading";
	static constexpr const char *TYPE_PARAGRAPH = "paragraph";
	static constexpr const char *TYPE_CODE = "code";
	static constexpr const char *TYPE_BLOCKQUOTE = "blockquote";
	static constexpr const char *TYPE_LIST = "list";
	static constexpr const char *TYPE_TABLE = "table";
	static constexpr const char *TYPE_HR = "hr";
	static constexpr const char *TYPE_METADATA = "metadata";
	static constexpr const char *TYPE_IMAGE = "image";
	static constexpr const char *TYPE_RAW = "raw";
	static constexpr const char *TYPE_DIV = "div";
	// A SEMANTIC sectioning container, distinct from `div`. HTML's spec calls div
	// "an element of last resort", so mapping <section>/<article>/<aside> onto it
	// would make element_type say something false while the truth hid in an
	// attribute. Which kind of section lives in attributes['role'] -- section,
	// article, aside, nav, header, footer, main -- following the convention set by
	// heading+heading_level and list+list_type rather than one type per variant.
	static constexpr const char *TYPE_SECTION = "section";
	static constexpr const char *TYPE_LINEBLOCK = "lineblock";
	static constexpr const char *TYPE_DEFLIST = "deflist";
	static constexpr const char *TYPE_FIGURE = "figure";
	static constexpr const char *TYPE_LIST_ITEM = "list_item";
	// A caption belonging to the container that precedes or follows it.
	// Deliberately general rather than figure-specific. POSITION IS THE EMITTER'S
	// CHOICE: a figure's caption follows its content, a <details> summary precedes
	// its body. `caption` marks what a block is, not where it sits.
	static constexpr const char *TYPE_CAPTION = "caption";
	// A structurally-valid element whose type is not in the standard vocabulary.
	// Distinct from TYPE_RAW, which is literal content in a *named* format. The
	// originating type name is preserved in attributes['source_type'].
	static constexpr const char *TYPE_GENERIC = "generic";

	// Inline element type names
	static constexpr const char *INLINE_TEXT = "text";
	static constexpr const char *INLINE_BOLD = "bold";
	static constexpr const char *INLINE_ITALIC = "italic";
	static constexpr const char *INLINE_CODE = "code";
	static constexpr const char *INLINE_LINK = "link";
	static constexpr const char *INLINE_IMAGE = "image";
	static constexpr const char *INLINE_SPACE = "space";
	static constexpr const char *INLINE_SOFTBREAK = "softbreak";
	static constexpr const char *INLINE_LINEBREAK = "linebreak";
	static constexpr const char *INLINE_STRIKETHROUGH = "strikethrough";
	static constexpr const char *INLINE_SUPERSCRIPT = "superscript";
	static constexpr const char *INLINE_SUBSCRIPT = "subscript";
	static constexpr const char *INLINE_UNDERLINE = "underline";
	static constexpr const char *INLINE_SMALLCAPS = "smallcaps";
	static constexpr const char *INLINE_SPAN = "span";
	static constexpr const char *INLINE_RAW = "raw";
	static constexpr const char *INLINE_MATH = "math";
	static constexpr const char *INLINE_QUOTED = "quoted";
	static constexpr const char *INLINE_CITE = "cite";
	static constexpr const char *INLINE_NOTE = "note";
	static constexpr const char *INLINE_GENERIC = "generic";

	// Valid encoding values
	static constexpr const char *ENCODING_TEXT = "text";
	static constexpr const char *ENCODING_JSON = "json";
	static constexpr const char *ENCODING_YAML = "yaml";
	static constexpr const char *ENCODING_HTML = "html";
	static constexpr const char *ENCODING_XML = "xml";
	static constexpr const char *ENCODING_MARKDOWN = "markdown";
	static constexpr const char *ENCODING_LATEX = "latex";

	// MIME type for frontmatter in HTML (RFC 9512 compliant)
	static constexpr const char *FRONTMATTER_MIME_TYPE = "application/vnd.frontmatter+yaml";

	// Attribute keys
	static constexpr const char *ATTR_HEADING_LEVEL = "heading_level";
	static constexpr const char *ATTR_ROLE = "role";
	static constexpr const char *ATTR_SOURCE_TYPE = "source_type";
	static constexpr const char *ATTR_LIST_TYPE = "list_type";

	// Helper to create an attributes MAP from a std::map
	static Value CreateAttributesMap(const std::map<std::string, std::string> &attrs) {
		vector<Value> keys;
		vector<Value> values;
		for (auto &entry : attrs) {
			keys.push_back(Value(entry.first));
			values.push_back(Value(entry.second));
		}
		return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
	}

	// Helper to create a doc_element Value (block kind)
	static Value CreateBlock(const std::string &element_type, const std::string &content, const Value &level,
	                         const std::string &encoding, const std::map<std::string, std::string> &attributes,
	                         int32_t element_order = 0) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(element_type)));
		// Empty content => NULL (spec convention for containers whose text lives
		// in structured inline children).
		struct_values.push_back(make_pair("content", content.empty() ? Value(LogicalType::VARCHAR) : Value(content)));
		struct_values.push_back(make_pair("level", level));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
		struct_values.push_back(make_pair("element_order", Value(element_order)));

		return Value::STRUCT(std::move(struct_values));
	}

	// Convenience overload for blocks without level
	static Value CreateBlock(const std::string &element_type, const std::string &content, const std::string &encoding,
	                         const std::map<std::string, std::string> &attributes, int32_t element_order = 0) {
		return CreateBlock(element_type, content, Value(), encoding, attributes, element_order);
	}

	// Helper to create an inline doc_element Value
	static Value CreateInline(const std::string &element_type, const std::string &content, const Value &level,
	                          const std::string &encoding, const std::map<std::string, std::string> &attributes,
	                          int32_t element_order = 0) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(KIND_INLINE)));
		struct_values.push_back(make_pair("element_type", Value(element_type)));
		// Empty content => NULL (a formatting container that recurses into
		// structured child inlines carries no literal content of its own).
		struct_values.push_back(make_pair("content", content.empty() ? Value(LogicalType::VARCHAR) : Value(content)));
		struct_values.push_back(make_pair("level", level));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
		struct_values.push_back(make_pair("element_order", Value(element_order)));

		return Value::STRUCT(std::move(struct_values));
	}
};

} // namespace duckdb
