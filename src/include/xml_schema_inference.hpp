#pragma once

#include "duckdb.hpp"
#include "xml_utils.hpp"
#include <libxml/parser.h>
#include <unordered_map>
#include <vector>

namespace duckdb {

// 4-tier priority system for XML element classification
enum class XMLTier {
	HOMOGENEOUS_CONFORMING = 1, // Can be mapped to clean DuckDB types (SCALAR, LIST, STRUCT with consistent structure)
	HETEROGENEOUS_CONFORMING = 2, // Inconsistent but extractable structure (STRUCT with mixed types, mixed arrays)
	EXTRACTABLE_AS_FRAGMENT = 3,  // Can be unwrapped as XMLFragment (no parent attributes, content-focused)
	FALLBACK_TO_XML = 4           // Must preserve full XML context (has parent attributes or complex nesting)
};

// Configuration options for schema inference
struct XMLSchemaOptions {
	// Schema inference controls
	std::string root_element; // Extract only children of specified root (empty = auto-detect)
	bool auto_detect = true;  // Automatic type detection
	int32_t max_depth = 10;   // Maximum introspection depth (default 10, hard cap at 20)
	int32_t sample_size = 50; // Number of elements to sample for inference

	// Attribute handling (aligned with xml_to_json)
	std::string attr_mode = "columns";   // 'columns' | 'prefixed' | 'map' | 'discard'
	std::string attr_prefix = "@";       // Prefix for attributes when attr_mode='prefixed'

	// Content handling (aligned with xml_to_json)
	std::string text_key = "#text";      // Key for mixed text content in structured types
	std::string tagname_key = "#tagname"; // Key for element tag names in heterogeneous records
	std::string namespaces = "strip";    // Namespace handling: 'strip' | 'expand' | 'keep'
	std::string empty_elements = "null"; // How to handle empty elements: 'null' | 'string' | 'object'
	bool preserve_mixed_content = false; // Handle elements with both text and children
	bool unnest_as_columns = true;       // True: flatten nested elements as columns, False: preserve as structs

	// Type detection
	bool temporal_detection = true; // Detect DATE/TIME/TIMESTAMP
	bool numeric_detection = true;  // Detect optimal numeric types
	bool boolean_detection = true;  // Detect boolean values

	// Collection handling
	double array_threshold = 0.8; // Minimum homogeneity for arrays (80%)
	int32_t max_array_depth = 3;  // Maximum nested array depth
	std::string record_element = ""; // XPath or tag name for elements that should be rows (e.g., "//item" or "item")
	std::string force_list = "";     // Comma-separated list of element names that should always be LIST type (like xml_to_json)

	// Error handling
	bool ignore_errors = false;         // Continue on parsing errors
	idx_t maximum_file_size = 16777216; // 16MB default

	// Type forcing
	bool all_varchar = false; // Force all scalar types to VARCHAR (nested structure preserved)
};

// Information about an inferred column
struct XMLColumnInfo {
	std::string name;                       // Column name
	LogicalType type;                       // DuckDB type
	bool is_attribute;                      // True if this comes from an XML attribute
	std::string xpath;                      // XPath to extract this column
	double confidence;                      // Schema inference confidence (0.0-1.0)
	std::vector<std::string> sample_values; // Sample values used for type detection

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
	std::vector<xmlNodePtr> instances; // All occurrences of this column across records
	int occurrence_count;              // Total times this appears
	bool repeats_in_record;            // True if any record has multiple instances

	ColumnAnalysis(const std::string &name, bool is_attribute = false)
	    : name(name), is_attribute(is_attribute), occurrence_count(0), repeats_in_record(false) {
	}
};

// Core schema inference engine
class XMLSchemaInference {
public:
	// Main inference function
	static std::vector<XMLColumnInfo> InferSchema(const std::string &xml_content,
	                                              const XMLSchemaOptions &options = XMLSchemaOptions {});

	// Extract structured data according to inferred schema
	static std::vector<std::vector<Value>> ExtractData(const std::string &xml_content,
	                                                   const XMLSchemaOptions &options = XMLSchemaOptions {});

	// Extract structured data according to explicit schema
	static std::vector<std::vector<Value>> ExtractDataWithSchema(const std::string &xml_content,
	                                                             const std::vector<std::string> &column_names,
	                                                             const std::vector<LogicalType> &column_types,
	                                                             const XMLSchemaOptions &options = XMLSchemaOptions {});

	// Analyze document structure and detect patterns
	static std::vector<ElementPattern> AnalyzeDocumentStructure(const std::string &xml_content,
	                                                            const XMLSchemaOptions &options);

	// Infer type from sample values
	static LogicalType InferTypeFromSamples(const std::vector<std::string> &samples, const XMLSchemaOptions &options);

	// Detect nested structures (LIST and STRUCT types)
	static LogicalType InferNestedType(const ElementPattern &pattern,
	                                   const std::unordered_map<std::string, ElementPattern> &all_patterns,
	                                   const XMLSchemaOptions &options);

	// Type detection helpers
	static bool IsBoolean(const std::string &value);
	static bool IsInteger(const std::string &value);
	static bool IsDouble(const std::string &value);
	static bool IsDate(const std::string &value);
	static bool IsTime(const std::string &value);
	static bool IsTimestamp(const std::string &value);

private:
	// 3-phase schema inference helpers
	static std::vector<xmlNodePtr> IdentifyRecordElements(XMLDocRAII &doc, xmlNodePtr root,
	                                                       const XMLSchemaOptions &options);
	static std::unordered_map<std::string, ColumnAnalysis> IdentifyColumns(
	    const std::vector<xmlNodePtr> &record_elements, const XMLSchemaOptions &options);
	static LogicalType InferColumnType(const ColumnAnalysis &column, int remaining_depth,
	                                   const XMLSchemaOptions &options);

	// Internal helper functions
	static void AnalyzeElement(xmlNodePtr node, std::unordered_map<std::string, ElementPattern> &patterns,
	                           const XMLSchemaOptions &options, int32_t current_depth = 0);

	static std::string GetElementXPath(const std::string &element_name, bool is_attribute = false,
	                                   const std::string &attribute_name = "");

	static std::string StripNamespacePrefix(const std::string &name);

	static LogicalType GetMostSpecificType(const std::vector<LogicalType> &types);

	static std::string CleanTextContent(const std::string &text);

	static Value ConvertToValue(const std::string &text, const LogicalType &target_type);

	// Recursive extraction helpers for complex types
	static Value ExtractValueFromNode(xmlNodePtr node, const LogicalType &target_type);
	static Value ExtractStructFromNode(xmlNodePtr node, const LogicalType &struct_type);
	static Value ExtractListFromNode(xmlNodePtr node, const LogicalType &list_type);
	static Value ExtractXMLArrayFromNode(xmlNodePtr node);
};

} // namespace duckdb
