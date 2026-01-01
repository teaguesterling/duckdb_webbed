#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"

namespace duckdb {

/**
 * DocBlockTypes provides type definitions and utilities for working with doc_block structures.
 *
 * This is a header-only interface that mirrors the duck_block_utils extension's type definitions,
 * enabling webbed to produce doc_block output without a compile-time dependency on duck_block_utils.
 *
 * The doc_block type represents a document block with the following structure:
 * STRUCT(
 *     block_type VARCHAR,      -- 'heading', 'paragraph', 'code', etc.
 *     content VARCHAR,         -- The block's text content
 *     level INTEGER,           -- Heading level (1-6), blockquote nesting, etc.
 *     encoding VARCHAR,        -- 'text', 'json', 'yaml', 'html', 'xml'
 *     attributes MAP(VARCHAR, VARCHAR),  -- Key-value metadata
 *     block_order INTEGER      -- Position in document (0-indexed)
 * )
 */
class DocBlockTypes {
public:
	// Create the doc_block type
	static LogicalType DocBlockType() {
		child_list_t<LogicalType> struct_children;
		struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
		struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
		struct_children.push_back(make_pair("level", LogicalType::INTEGER));
		struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
		struct_children.push_back(
		    make_pair("attributes", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
		struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));

		return LogicalType::STRUCT(std::move(struct_children));
	}

	// Create a LIST(doc_block) type
	static LogicalType DocBlockListType() {
		return LogicalType::LIST(DocBlockType());
	}

	// Field indices for doc_block struct
	static constexpr idx_t BLOCK_TYPE_IDX = 0;
	static constexpr idx_t CONTENT_IDX = 1;
	static constexpr idx_t LEVEL_IDX = 2;
	static constexpr idx_t ENCODING_IDX = 3;
	static constexpr idx_t ATTRIBUTES_IDX = 4;
	static constexpr idx_t BLOCK_ORDER_IDX = 5;

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

	// Valid encoding values
	static constexpr const char *ENCODING_TEXT = "text";
	static constexpr const char *ENCODING_JSON = "json";
	static constexpr const char *ENCODING_YAML = "yaml";
	static constexpr const char *ENCODING_HTML = "html";
	static constexpr const char *ENCODING_XML = "xml";

	// MIME type for frontmatter in HTML (RFC 9512 compliant)
	static constexpr const char *FRONTMATTER_MIME_TYPE = "application/vnd.frontmatter+yaml";

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

	// Helper to create a doc_block Value
	static Value CreateBlock(const std::string &block_type, const std::string &content, const Value &level,
	                         const std::string &encoding, const std::map<std::string, std::string> &attributes,
	                         int32_t block_order = 0) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("block_type", Value(block_type)));
		struct_values.push_back(make_pair("content", Value(content)));
		struct_values.push_back(make_pair("level", level));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
		struct_values.push_back(make_pair("block_order", Value(block_order)));

		return Value::STRUCT(std::move(struct_values));
	}

	// Convenience overload for blocks without level
	static Value CreateBlock(const std::string &block_type, const std::string &content, const std::string &encoding,
	                         const std::map<std::string, std::string> &attributes, int32_t block_order = 0) {
		return CreateBlock(block_type, content, Value(), encoding, attributes, block_order);
	}
};

} // namespace duckdb
