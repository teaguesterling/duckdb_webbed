#pragma once

#include "duckdb.hpp"
#include "xml_utils.hpp"
#include <libxml/parser.h>
#include <unordered_map>
#include <vector>
#include "duckdb/function/scalar/strftime_format.hpp"

namespace duckdb {

// 4-tier priority system for XML element classification
enum class XMLTier {
	HOMOGENEOUS_CONFORMING = 1, // Can be mapped to clean DuckDB types (SCALAR, LIST, STRUCT with consistent structure)
	HETEROGENEOUS_CONFORMING = 2, // Inconsistent but extractable structure (STRUCT with mixed types, mixed arrays)
	EXTRACTABLE_AS_FRAGMENT = 3,  // Can be unwrapped as XMLFragment (no parent attributes, content-focused)
	FALLBACK_TO_XML = 4           // Must preserve full XML context (has parent attributes or complex nesting)
};

// One force_list entry. Steps are element names from outer to inner. A relative pattern (`a/b`)
// is anchored at the record element. A descendant pattern (`//a/b`, or a bare name) matches at any
// depth.
struct ForceListPattern {
	bool descendant = false;
	std::vector<std::string> steps;
};

// Parses the force_list parameter (VARCHAR or LIST of VARCHAR). Throws BinderException on
// unsupported XPath.
std::vector<ForceListPattern> ParseForceList(const Value &value);

// Configuration options for schema inference
struct XMLSchemaOptions {
	// Schema inference controls
	std::string root_element;    // Extract only children of specified root (empty = auto-detect)
	bool auto_detect = true;     // Automatic type detection
	int32_t max_depth = 10;      // Maximum introspection depth (default 10, hard cap at 20)
	int32_t sample_size = 10240; // Sampled values per field for type inference (records, in SAX mode).
	                             // Large default so a late outlier still within the window is seen during
	                             // inference and the column is typed to fit it (e.g. VARCHAR); a value
	                             // beyond the window can still fail to cast. -1 samples every value (DOM
	                             // path). (issue #102)

	// Attribute handling (aligned with xml_to_json)
	std::string attr_mode = "columns"; // 'columns' | 'prefixed' | 'map' | 'discard'
	std::string attr_prefix = "@";     // Prefix for attributes when attr_mode='prefixed'

	// Content handling (aligned with xml_to_json)
	std::string text_key = "#text";       // Key for mixed text content in structured types
	std::string tagname_key = "#tagname"; // Key for element tag names in heterogeneous records
	std::string namespaces = "strip";     // Namespace handling: 'strip' | 'expand' | 'keep'
	std::string empty_elements = "null";  // How to handle empty elements: 'null' | 'string' | 'object'
	bool preserve_mixed_content = false;  // Handle elements with both text and children
	bool unnest_as_columns = true;        // True: flatten nested elements as columns, False: preserve as structs

	// Type detection
	bool temporal_detection = true; // Detect DATE/TIME/TIMESTAMP
	bool numeric_detection = true;  // Detect optimal numeric types
	bool boolean_detection = true;  // Detect boolean values
	// Extraction into an INTEGER, BIGINT, or DOUBLE column rejects text the type cannot reproduce
	// (`007`, `1.10`, `1e3`), the same lexemes inference refuses to type as numeric. Off for
	// `columns :=` types, which cast.
	bool lossless_numeric_text = true;
	// numeric_inference := 'exact' (the default): a column is numeric only when its type reproduces every lexeme.
	// 'lenient' types by DuckDB's cast, as before #221, so `1.10` is DOUBLE 1.1.
	bool exact_numeric = true;

	// Datetime format (replaces temporal_detection for explicit format control)
	std::vector<std::string>
	    datetime_format_candidates;            // Resolved format strings (empty = use temporal_detection flag)
	bool has_explicit_datetime_format = false; // User specified a format (not 'auto')

	// Collection handling
	double array_threshold = 0.8;    // Minimum homogeneity for arrays (80%)
	int32_t max_array_depth = 3;     // Maximum nested array depth
	std::string record_element = ""; // XPath or tag name for elements that should be rows (e.g., "//item" or "item")
	std::vector<ForceListPattern> force_list; // Elements that should always be LIST type

	// Error handling
	bool ignore_errors = false;         // Continue on parsing errors
	idx_t maximum_file_size = 16777216; // 16MB default (files above this use SAX streaming)
	string compression = "auto";
	idx_t max_record_bytes = 0; // Input bytes one streamed record may span (0 = unbounded)

	// SAX streaming controls
	bool streaming = true; // Enable SAX mode for files exceeding maximum_file_size (default: true)

	// Whitespace handling
	bool preserve_whitespace = true; // Preserve internal whitespace in text content (default: true for v2.0.0)

	// Type forcing
	bool all_varchar = false;              // Force all scalar types to VARCHAR (nested structure preserved)
	std::vector<std::string> null_strings; // Values to treat as NULL (empty = default behavior)
	// Scalar types inference may choose (read_csv's auto_type_candidates). Empty allows every type
	// inference produces. VARCHAR is always allowed, whether listed or not.
	std::vector<LogicalTypeId> auto_type_candidates;

	// Source type tracking
	std::string opaque_type_name = "XML"; // Type name to use for opaque elements: "XML" or "HTML"
	// How HTML content is decoded. read_html sets FILE_BYTES. parse_html's content is a decoded value.
	HTMLInputEncoding html_input = HTMLInputEncoding::DECODED_TEXT;
};

// Information about an inferred column
struct XMLColumnInfo {
	std::string name;                       // Column name
	LogicalType type;                       // DuckDB type
	bool is_attribute;                      // True if this comes from an XML attribute
	std::string xpath;                      // XPath to extract this column
	double confidence;                      // Schema inference confidence (0.0-1.0)
	std::vector<std::string> sample_values; // Sample values used for type detection
	std::string winning_datetime_format;    // Format string that won during inference (empty = no format)
	bool integers_fit_double = true;        // Every sampled integer is exact in a DOUBLE (under 2^53)

	XMLColumnInfo(const std::string &name, LogicalType type, bool is_attribute = false, const std::string &xpath = "",
	              double confidence = 1.0)
	    : name(name), type(type), is_attribute(is_attribute), xpath(xpath), confidence(confidence) {
	}
};

// Statistics about element patterns
struct ElementPattern {
	std::string name;                                          // Element name
	int32_t occurrence_count = 0;                              // How many times this element appears
	std::vector<std::string> sample_values;                    // Sample text values
	std::unordered_map<std::string, int32_t> attribute_counts; // Attribute frequency
	bool has_children = false;                                 // Has child elements
	bool has_text = false;                                     // Has text content

	// Enhanced nested structure tracking
	std::unordered_map<std::string, int32_t> child_element_counts;              // Child element frequency
	std::vector<std::unordered_map<std::string, std::string>> child_structures; // Sample child structures
	bool appears_in_array = false;         // This element appears multiple times with same parent
	bool has_homogeneous_structure = true; // All instances have same structure

	// Computed flags for 6-tier priority system
	bool is_scalar = false;                   // Only text content, no children
	bool all_children_same_name = false;      // All children have identical names
	bool all_children_different_name = false; // All children have unique names (no repeats)
	bool has_attributes = false;              // This node has attributes
	bool children_have_attributes = false;    // Any child has attributes
	bool all_children_conforming = false;     // All children can be consistently typed

	double GetFrequency(int32_t total_samples) const {
		return total_samples > 0 ? static_cast<double>(occurrence_count) / total_samples : 0.0;
	}

	bool IsListCandidate() const {
		return appears_in_array && has_homogeneous_structure;
	}

	bool IsStructCandidate() const {
		return has_children && !has_text && child_element_counts.size() > 0;
	}

	// 4-tier priority system detection
	XMLTier GetTier() const {
		// Tier 1: Homogeneous conforming - can map to clean DuckDB types
		if (is_scalar && !has_attributes) {
			// Pure scalar content (VARCHAR, INTEGER, etc.)
			return XMLTier::HOMOGENEOUS_CONFORMING;
		}
		if (all_children_same_name && !has_attributes && all_children_conforming) {
			// Homogeneous array of same type (LIST<T>)
			return XMLTier::HOMOGENEOUS_CONFORMING;
		}
		if (all_children_different_name && all_children_conforming && !has_attributes) {
			// Consistent struct with predictable schema (STRUCT)
			return XMLTier::HOMOGENEOUS_CONFORMING;
		}

		// Tier 2: Heterogeneous conforming - extractable but inconsistent
		if (has_children && all_children_conforming) {
			// Mixed structure but children are individually conforming
			return XMLTier::HETEROGENEOUS_CONFORMING;
		}

		// Tier 3: Extractable as fragment - can unwrap without parent context
		if (!has_attributes) {
			// No parent attributes means we can safely unwrap content
			return XMLTier::EXTRACTABLE_AS_FRAGMENT;
		}

		// Tier 4: Fall back to XML - must preserve full context
		return XMLTier::FALLBACK_TO_XML;
	}
};

// Information about a column during schema inference (used in Phase 2)
struct ColumnAnalysis {
	std::string name;
	bool is_attribute;
	std::vector<xmlNodePtr> instances;  // All occurrences of this column across records
	int occurrence_count;               // Total times this appears
	bool repeats_in_record;             // True if any record has multiple instances
	std::vector<std::string> ancestors; // Record element, then each element down to this one's parent

	ColumnAnalysis(const std::string &name, bool is_attribute = false)
	    : name(name), is_attribute(is_attribute), occurrence_count(0), repeats_in_record(false) {
	}
};

// Core schema inference engine
class XMLSchemaInference {
public:
	// Main inference function. An element empty in every instance types as VARCHAR.
	static std::vector<XMLColumnInfo> InferSchema(const std::string &xml_content,
	                                              const XMLSchemaOptions &options = XMLSchemaOptions {});

	// Inference for a schema that will be merged with other files' schemas. An element empty in
	// every instance keeps SQLNULL so the merge can take its type from another file. Pass the merged
	// types through ResolveUnknownTypes before using them.
	static std::vector<XMLColumnInfo> InferSchemaForMerge(const std::string &xml_content,
	                                                      const XMLSchemaOptions &options);
	// InferSchemaForMerge over an already parsed document, for callers that parsed it to check validity.
	static std::vector<XMLColumnInfo> InferSchemaFromDocument(XMLDocRAII &doc, const XMLSchemaOptions &options);

	// Extract structured data according to inferred schema
	static std::vector<std::vector<Value>> ExtractData(const std::string &xml_content,
	                                                   const XMLSchemaOptions &options = XMLSchemaOptions {});

	// Extract structured data according to explicit schema
	static std::vector<std::vector<Value>>
	ExtractDataWithSchema(const std::string &xml_content, const std::vector<std::string> &column_names,
	                      const std::vector<LogicalType> &column_types,
	                      const XMLSchemaOptions &options = XMLSchemaOptions {},
	                      const std::vector<std::string> &column_datetime_formats = {});

	// Write one record into row of out starting at column col, returning the column after the last one written
	static idx_t WriteRecord(xmlNodePtr record, const std::vector<XMLColumnInfo> &schema, int remaining_depth,
	                         const XMLSchemaOptions &options, DataChunk &out, idx_t col, idx_t row);
	static idx_t WriteRecordWithSchema(xmlNodePtr record, const std::vector<std::string> &column_names,
	                                   const std::vector<LogicalType> &column_types, const XMLSchemaOptions &options,
	                                   const std::vector<std::string> &column_datetime_formats, DataChunk &out,
	                                   idx_t col, idx_t row);

	// Identify record elements in a parsed document
	static std::vector<xmlNodePtr> IdentifyRecordElements(XMLDocRAII &doc, xmlNodePtr root,
	                                                      const XMLSchemaOptions &options);

	// Analyze document structure and detect patterns
	static std::vector<ElementPattern> AnalyzeDocumentStructure(const std::string &xml_content,
	                                                            const XMLSchemaOptions &options);

	// Infer type from sample values
	static LogicalType InferTypeFromSamples(const std::vector<std::string> &samples, const XMLSchemaOptions &options,
	                                        std::string &winning_datetime_format);

	// Whether inference may choose this scalar type under options.auto_type_candidates.
	static bool IsAutoTypeCandidate(const XMLSchemaOptions &options, LogicalTypeId id);

	// The type a datetime format of this type yields under auto_type_candidates: itself, or
	// TIMESTAMP for an excluded DATE. INVALID when neither is allowed.
	static LogicalType AllowedTemporalType(const XMLSchemaOptions &options, const LogicalType &type);

	// Replace every scalar inferable type outside options.auto_type_candidates with VARCHAR,
	// including inside nested types, so a union_by_name merge cannot widen past the candidates.
	static LogicalType RestrictToAutoTypeCandidates(const LogicalType &type, const XMLSchemaOptions &options);

	// Detect nested structures (LIST and STRUCT types)
	static LogicalType InferNestedType(const ElementPattern &pattern,
	                                   const std::unordered_map<std::string, ElementPattern> &all_patterns,
	                                   const XMLSchemaOptions &options);

	// Type detection helpers
	static bool IsBoolean(const std::string &value);
	static bool IsInteger(const std::string &value, bool exact = true);
	static bool IsDouble(const std::string &value, bool exact = true);

	// Datetime format helpers
	static LogicalType ClassifyDatetimeFormat(const std::string &format);
	static void ValidateDatetimeFormatString(const std::string &format);

	// Public wrapper for ConvertToValue (used by SAX streaming reader)
	static Value ConvertToValuePublic(const std::string &text, const LogicalType &target_type,
	                                  const XMLSchemaOptions &options, const std::string &datetime_format = "");

	// Trim edges, optionally normalize EOL or collapse whitespace (used by both DOM and SAX paths)
	static std::string CleanTextContent(const std::string &text, bool preserve_whitespace);
	static std::string CleanTextContent(const char *text, size_t size, bool preserve_whitespace);
	static std::string CleanNodeText(xmlNodePtr node, bool preserve_whitespace);

	// Map a column name back to the underlying XML attribute name. In attr_mode='prefixed' the
	// inferred column name is attr_prefix + attribute name, so the prefix must be stripped before
	// looking up the attribute on the node (used by both DOM and SAX extraction paths).
	static std::string AttributeNameFromColumn(const std::string &column_name, const XMLSchemaOptions &options);

	// xsi:nil="true" elements are treated as empty by both read paths. The xsi:nil attribute
	// never becomes a column. Other xsi: attributes stay ordinary attributes.
	static constexpr const char *XSI_NAMESPACE_URI = "http://www.w3.org/2001/XMLSchema-instance";
	// True for the xs:boolean lexical forms of true ("true" or "1", surrounding whitespace allowed)
	static bool IsXsiNilTrue(const char *value);
	// Remove every xsi:nil attribute, and the content of each element where it is true
	static void ApplyXsiNil(xmlDocPtr doc);
	// The same for one element's subtree, such as a streamed record
	static void ApplyXsiNil(xmlNodePtr root);

	// Merge two column types from union_by_name inference across files.
	// Recursively unions STRUCT fields, recurses into LIST element types, falls back to VARCHAR
	// when a complex type collides with a non-matching kind. Scalar promotion uses
	// LogicalType::ForceMaxLogicalType but only accepts the candidate when both inputs have a
	// valid implicit cast path to it (via CastRules::ImplicitCast); otherwise falls back to
	// VARCHAR so textual data is preserved.
	// SQLNULL is the inferred type of an element that is empty in every instance (no text, children
	// or attributes) and merges as the identity.
	// A scalar merged with a STRUCT that has a text_key field (an element seen as plain text in one
	// file and as text plus attributes in another) widens into that field.
	static LogicalType MergeXMLColumnType(const LogicalType &a, const LogicalType &b, const std::string &text_key,
	                                      bool exact_numeric = true);

	// Replace SQLNULL left over after merging (an element empty in every file) with VARCHAR,
	// including inside nested types.
	static LogicalType ResolveUnknownTypes(const LogicalType &type);
	static void ResolveUnknownTypes(std::vector<XMLColumnInfo> &columns);

private:
	// 3-phase schema inference helpers
	// Returns columns in first-seen document order so the inferred column order is deterministic.
	static std::vector<ColumnAnalysis> IdentifyColumns(const std::vector<xmlNodePtr> &record_elements,
	                                                   const XMLSchemaOptions &options);
	static bool SampledIntegersFitDouble(const ColumnAnalysis &column, const XMLSchemaOptions &options);
	static LogicalType InferColumnType(const ColumnAnalysis &column, int remaining_depth,
	                                   const XMLSchemaOptions &options, std::string &winning_datetime_format);

	// Internal helper functions
	static void AnalyzeElement(xmlNodePtr node, std::unordered_map<std::string, ElementPattern> &patterns,
	                           const XMLSchemaOptions &options, int32_t current_depth = 0);

	static std::string GetElementXPath(const std::string &element_name, bool is_attribute = false,
	                                   const std::string &attribute_name = "");

	static std::string StripNamespacePrefix(const std::string &name);

	// Child elements across all instances, grouped by name in first-seen order, with each name's most
	// occurrences in one instance.
	struct ChildGroups {
		std::vector<std::string> names;
		std::vector<std::vector<xmlNodePtr>> nodes;
		std::vector<int> max_counts;
	};
	static ChildGroups GroupChildElements(const std::vector<xmlNodePtr> &instances, const XMLSchemaOptions &options);

	// Attribute names across all instances in first-seen order, empty under attr_mode 'discard'. A name
	// already taken by a child element is skipped, since a STRUCT can't hold two fields with one name.
	static std::vector<std::string> CollectAttributeNames(const std::vector<xmlNodePtr> &instances,
	                                                      const XMLSchemaOptions &options,
	                                                      const std::vector<std::string> &child_names);

	static LogicalType GetMostSpecificType(const std::vector<LogicalType> &types);

	static Value ConvertToValue(const std::string &text, const LogicalType &target_type,
	                            const XMLSchemaOptions &options, const std::string &datetime_format = "");

	// Recursive extraction helpers for complex types.
	// datetime_format is the column's winning format, threaded down so nested STRUCT fields and
	// LIST elements convert with the same format as the column's own scalar values (issue #159).
	// Empty means none was recorded, in which case the conversion falls back to the candidate
	// list inference used.
	static void WriteNodeValue(xmlNodePtr node, const LogicalType &target_type, Vector &out, idx_t row,
	                           const XMLSchemaOptions &options, const std::string &datetime_format = "");
	static void WriteScalarFromNode(xmlNodePtr node, const LogicalType &target_type, Vector &out, idx_t row,
	                                const XMLSchemaOptions &options, const std::string &datetime_format);
	static void WriteConverted(const std::string &text, const LogicalType &target_type, Vector &out, idx_t row,
	                           const XMLSchemaOptions &options, const std::string &datetime_format);
	static void WriteStructFromNode(xmlNodePtr node, const LogicalType &struct_type, Vector &out, idx_t row,
	                                const XMLSchemaOptions &options, const std::string &datetime_format);
	static void WriteListFromNode(xmlNodePtr node, const LogicalType &list_type, Vector &out, idx_t row,
	                              const XMLSchemaOptions &options, const std::string &datetime_format);
	static void WriteChildList(xmlNodePtr record, const std::string &name, const LogicalType &list_type, Vector &out,
	                           idx_t row, const XMLSchemaOptions &options, const std::string &datetime_format);
	static Value ExtractXMLArrayFromNode(xmlNodePtr node);
	static void AppendListItems(xmlNodePtr node, const LogicalType &element_type, Vector &list,
	                            const XMLSchemaOptions &options, const std::string &datetime_format);
	static void AppendListItem(xmlNodePtr node, const LogicalType &element_type, Vector &list,
	                           const XMLSchemaOptions &options, const std::string &datetime_format);
};

} // namespace duckdb
