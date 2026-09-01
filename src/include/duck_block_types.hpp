#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"
// Canonical duck_block vocabulary, taken as a submodule (duck_block_utils/) rather
// than copied. Header-only and link-free: it declares nothing it does not define,
// so nothing from duck_block_utils needs linking.
#include "duck_block_vocabulary.hpp"

namespace duckdb {

/**
 * DuckBlockTypes provides type definitions and utilities for working with doc_element structures.
 *
 * The vocabulary itself -- kind values, element type names, encodings, field indices --
 * comes from DuckBlockVocabulary in the duck_block_utils submodule, which this class
 * inherits so every existing DuckBlockTypes::TYPE_* reference keeps resolving. Only
 * webbed-specific additions are declared below. This replaces a hand-maintained copy
 * that had silently drifted to 29 of 44 names.
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
class DuckBlockTypes : public DuckBlockVocabulary {
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

	// ------------------------------------------------------------------
	// webbed-specific additions. Everything else -- KIND_*, TYPE_*, INLINE_*,
	// ENCODING_*, the field indices -- is inherited from DuckBlockVocabulary.
	// ------------------------------------------------------------------

	// Attribute keys. The canonical header documents these as prose inside its
	// comments (attributes['role'] and so on) but declares no ATTR_* constants,
	// so they are named here to keep the string literals in one place.
	static constexpr const char *ATTR_HEADING_LEVEL = "heading_level";
	static constexpr const char *ATTR_ROLE = "role";
	static constexpr const char *ATTR_SOURCE_TYPE = "source_type";
	static constexpr const char *ATTR_LIST_TYPE = "list_type";

	// MIME type for frontmatter in HTML (RFC 9512 compliant). HTML-specific, so
	// it has no counterpart in the format-neutral canonical vocabulary.
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
