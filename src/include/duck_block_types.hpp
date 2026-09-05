#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"
// Canonical duck_block vocabulary, vendored from duck_block_utils (see the header
// comment in duck_block_vocabulary.hpp for the upstream commit and drift check).
// Header-only and link-free: it declares nothing it does not define, so nothing
// from duck_block_utils needs linking.
#include "duck_block_vocabulary.hpp"

namespace duckdb {

/**
 * DuckBlockTypes provides type definitions and utilities for working with doc_element structures.
 *
 * The vocabulary itself -- kind values, element type names, encodings, field indices --
 * comes from DuckBlockVocabulary, vendored from duck_block_utils in
 * src/include/duck_block_vocabulary.hpp, which this class inherits so every existing
 * DuckBlockTypes::TYPE_* reference keeps resolving. Only webbed-specific additions are
 * declared below. This replaces a hand-maintained copy that had silently drifted to 29
 * of 44 names; scripts/check_duck_block_vocabulary.py guards against the vendored copy
 * drifting the same way.
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

	// The ONE widened shape duck_block spec 6.4 accepts: the canonical seven,
	// then a trailing `filename VARCHAR`. Derived from DuckBlockType() rather
	// than restated, so the two cannot drift. Consumers accept this shape via a
	// registered implicit cast (see DuckBlockFunctions::Register); producers may
	// emit it (read_html_blocks does, behind filename := true).
	//
	// The bare "filename" literal becomes FIELD_FILENAME once the vendored
	// vocabulary is re-pinned to 6.4, which publishes it; the value is ruled and
	// will not change, only the spelling of where it comes from.
	static LogicalType DuckBlockWithFilenameType() {
		auto children = StructType::GetChildTypes(DuckBlockType());
		children.push_back(make_pair("filename", LogicalType::VARCHAR));
		return LogicalType::STRUCT(std::move(children));
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

	// Attribute keys. ATTR_HEADING_LEVEL, ATTR_ROLE, and ATTR_SOURCE_TYPE now come
	// from the vendored DuckBlockVocabulary (inherited above), which as of
	// upstream fca9fb0 declares its own ATTR_* constants instead of leaving them
	// as prose in comments. ATTR_LIST_TYPE and ATTR_KEY are also inherited and
	// available for the emission work.

	// MIME type for frontmatter in HTML (RFC 9512 compliant). HTML-specific, so
	// it has no counterpart in the format-neutral canonical vocabulary.
	static constexpr const char *FRONTMATTER_MIME_TYPE = "application/vnd.frontmatter+yaml";


	// Helper to create an attributes MAP from a std::map. Key order in the
	// resulting MAP follows std::map's sorted iteration (alphabetical by key),
	// which is fine for callers that only ever look attributes up by name.
	static Value CreateAttributesMap(const std::map<std::string, std::string> &attrs) {
		vector<Value> keys;
		vector<Value> values;
		for (auto &entry : attrs) {
			keys.push_back(Value(entry.first));
			values.push_back(Value(entry.second));
		}
		return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
	}

	// Overload for callers where attribute DISPLAY ORDER is part of the
	// contract (e.g. role before id before class on a section block) and
	// std::map's alphabetical sort would scramble it. Preserves caller
	// insertion order.
	static Value CreateAttributesMap(const std::vector<std::pair<std::string, std::string>> &attrs) {
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

	// Level-omitting overload -- DELETED. Spec 3.0 onward requires every element to
	// carry an explicit level (top level 1, never NULL); this overload existed only
	// because it was one argument shorter than the one above, and that made it the
	// overload a hurried call site reached for. It cost the metadata emission path
	// a silent NULL level for three spec versions with nothing in a position to
	// object -- see the commit that deleted this. Every caller must pass a level
	// explicitly via the five-argument overload above.
	static Value CreateBlock(const std::string &element_type, const std::string &content, const std::string &encoding,
	                         const std::map<std::string, std::string> &attributes, int32_t element_order = 0) = delete;

	// Order-preserving overload, matching the CreateAttributesMap overload above.
	static Value CreateBlock(const std::string &element_type, const std::string &content, const Value &level,
	                         const std::string &encoding,
	                         const std::vector<std::pair<std::string, std::string>> &attributes,
	                         int32_t element_order = 0) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(element_type)));
		struct_values.push_back(make_pair("content", content.empty() ? Value(LogicalType::VARCHAR) : Value(content)));
		struct_values.push_back(make_pair("level", level));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
		struct_values.push_back(make_pair("element_order", Value(element_order)));

		return Value::STRUCT(std::move(struct_values));
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

	// Order-preserving overload, matching the CreateBlock overload above --
	// see CreateAttributesMap's ordered variant. Used so an inline element's
	// attributes (e.g. an <img>'s src/alt/title) preserve the same order the
	// block-side reader uses, rather than std::map's alphabetical order.
	static Value CreateInline(const std::string &element_type, const std::string &content, const Value &level,
	                          const std::string &encoding,
	                          const std::vector<std::pair<std::string, std::string>> &attributes,
	                          int32_t element_order = 0) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(KIND_INLINE)));
		struct_values.push_back(make_pair("element_type", Value(element_type)));
		struct_values.push_back(make_pair("content", content.empty() ? Value(LogicalType::VARCHAR) : Value(content)));
		struct_values.push_back(make_pair("level", level));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
		struct_values.push_back(make_pair("element_order", Value(element_order)));

		return Value::STRUCT(std::move(struct_values));
	}
};

} // namespace duckdb
