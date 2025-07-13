#pragma once

#include "duckdb.hpp"
#include "xml_utils.hpp"
#include <libxml/parser.h>
#include <unordered_map>
#include <vector>

namespace duckdb {

// 7-tier priority system for XML element classification
enum class XMLTier {
	UNKNOWN = 0,       // Unable to classify or invalid pattern
	SCALAR = 1,        // Homogeneous conforming scalar (only text content, no attributes)
	LIST = 2,          // Homogeneous conforming children (repeated same-name children, no parent attributes)  
	STRUCT = 3,        // Heterogeneous conforming children (different-name children, all conforming)
	XML_ARRAY = 4,     // Well-formed XML elements (heterogeneous but extractable as XML[], no parent attributes)
	XML_FRAGMENT = 5,  // Extractable as fragment (parent can be removed, no parent attributes)
	XML = 6            // Fall back to XML (default case, preserves all context)
};

// Configuration options for schema inference
struct XMLSchemaOptions {
	// Schema inference controls
	std::string root_element;           // Extract only children of specified root (empty = auto-detect)
	bool auto_detect = true;            // Automatic type detection
	int32_t schema_depth = 3;           // How deep to analyze (1-4)
	int32_t sample_size = 50;           // Number of elements to sample for inference
	
	// Attribute handling
	bool include_attributes = true;     // Include attributes as columns
	std::string attribute_prefix = "";  // Prefix for attribute columns (e.g., 'attr_')
	std::string attribute_mode = "columns"; // 'columns' | 'map' | 'discard'
	
	// Content handling
	std::string text_content_column = "text_content"; // Column name for mixed text content
	bool preserve_mixed_content = false; // Handle elements with both text and children
	
	// Type detection
	bool temporal_detection = true;     // Detect DATE/TIME/TIMESTAMP
	bool numeric_detection = true;      // Detect optimal numeric types
	bool boolean_detection = true;      // Detect boolean values
	
	// Collection handling
	double array_threshold = 0.8;       // Minimum homogeneity for arrays (80%)
	int32_t max_array_depth = 3;        // Maximum nested array depth
	
	// Error handling
	bool ignore_errors = false;         // Continue on parsing errors
	idx_t maximum_file_size = 16777216; // 16MB default
};

// Information about an inferred column
struct XMLColumnInfo {
	std::string name;                   // Column name
	LogicalType type;                   // DuckDB type
	bool is_attribute;                  // True if this comes from an XML attribute
	std::string xpath;                  // XPath to extract this column
	double confidence;                  // Schema inference confidence (0.0-1.0)
	std::vector<std::string> sample_values; // Sample values used for type detection
	
	XMLColumnInfo(const std::string& name, LogicalType type, bool is_attribute = false, 
	              const std::string& xpath = "", double confidence = 1.0)
		: name(name), type(type), is_attribute(is_attribute), xpath(xpath), confidence(confidence) {}
};

// Statistics about element patterns
struct ElementPattern {
	std::string name;                   // Element name
	int32_t occurrence_count = 0;       // How many times this element appears
	std::vector<std::string> sample_values; // Sample text values
	std::unordered_map<std::string, int32_t> attribute_counts; // Attribute frequency
	bool has_children = false;          // Has child elements
	bool has_text = false;              // Has text content
	
	// Enhanced nested structure tracking
	std::unordered_map<std::string, int32_t> child_element_counts; // Child element frequency
	std::vector<std::unordered_map<std::string, std::string>> child_structures; // Sample child structures
	bool appears_in_array = false;      // This element appears multiple times with same parent
	bool has_homogeneous_structure = true; // All instances have same structure
	
	// Computed flags for 6-tier priority system
	bool is_scalar = false;             // Only text content, no children
	bool all_children_same_name = false; // All children have identical names
	bool all_children_different_name = false; // All children have unique names (no repeats)
	bool has_attributes = false;        // This node has attributes
	bool children_have_attributes = false; // Any child has attributes
	bool all_children_conforming = false; // All children can be consistently typed
	
	double GetFrequency(int32_t total_samples) const {
		return total_samples > 0 ? static_cast<double>(occurrence_count) / total_samples : 0.0;
	}
	
	bool IsListCandidate() const {
		return appears_in_array && has_homogeneous_structure;
	}
	
	bool IsStructCandidate() const {
		return has_children && !has_text && child_element_counts.size() > 0;
	}
	
	// 7-tier priority system detection
	XMLTier GetTier() const {
		// Tier 0: Unknown/invalid patterns
		if (name.empty()) return XMLTier::UNKNOWN;
		
		// Tier 1: Homogeneous conforming scalar
		if (is_scalar && !has_attributes) return XMLTier::SCALAR;
		
		// Tier 2: Homogeneous conforming children (LIST)
		if (all_children_same_name && !has_attributes && all_children_conforming) return XMLTier::LIST;
		
		// Tier 3: Heterogeneous conforming children (STRUCT)
		if (all_children_different_name && all_children_conforming) return XMLTier::STRUCT;
		
		// Tier 4: Well-formed XML elements (XML[])
		if (!has_attributes && has_children) return XMLTier::XML_ARRAY;
		
		// Tier 5: Extractable as fragment (XMLFragment)
		if (!has_attributes) return XMLTier::XML_FRAGMENT;
		
		// Tier 6: Fall back to XML
		return XMLTier::XML;
	}
};

// Core schema inference engine
class XMLSchemaInference {
public:
	// Main inference function
	static std::vector<XMLColumnInfo> InferSchema(const std::string& xml_content, 
	                                               const XMLSchemaOptions& options = XMLSchemaOptions{});
	
	// Extract structured data according to inferred schema
	static std::vector<std::vector<Value>> ExtractData(const std::string& xml_content,
	                                                     const XMLSchemaOptions& options = XMLSchemaOptions{});
	
	// Extract structured data according to explicit schema
	static std::vector<std::vector<Value>> ExtractDataWithSchema(const std::string& xml_content,
	                                                              const std::vector<std::string>& column_names,
	                                                              const std::vector<LogicalType>& column_types,
	                                                              const XMLSchemaOptions& options = XMLSchemaOptions{});
	
	// Analyze document structure and detect patterns
	static std::vector<ElementPattern> AnalyzeDocumentStructure(const std::string& xml_content,
	                                                             const XMLSchemaOptions& options);
	
	// Infer type from sample values
	static LogicalType InferTypeFromSamples(const std::vector<std::string>& samples,
	                                         const XMLSchemaOptions& options);
	
	// Detect nested structures (LIST and STRUCT types)
	static LogicalType InferNestedType(const ElementPattern& pattern,
	                                    const std::unordered_map<std::string, ElementPattern>& all_patterns,
	                                    const XMLSchemaOptions& options);
	
	// Type detection helpers
	static bool IsBoolean(const std::string& value);
	static bool IsInteger(const std::string& value);
	static bool IsDouble(const std::string& value);
	static bool IsDate(const std::string& value);
	static bool IsTime(const std::string& value);
	static bool IsTimestamp(const std::string& value);
	
private:
	// Internal helper functions
	static void AnalyzeElement(xmlNodePtr node, std::unordered_map<std::string, ElementPattern>& patterns,
	                           const XMLSchemaOptions& options, int32_t current_depth = 0);
	
	static std::string GetElementXPath(const std::string& element_name, bool is_attribute = false,
	                                   const std::string& attribute_name = "");
	
	static LogicalType GetMostSpecificType(const std::vector<LogicalType>& types);
	
	static std::string CleanTextContent(const std::string& text);
	
	static Value ConvertToValue(const std::string& text, const LogicalType& target_type);
	
	// Recursive extraction helpers for complex types
	static Value ExtractValueFromNode(xmlNodePtr node, const LogicalType& target_type);
	static Value ExtractStructFromNode(xmlNodePtr node, const LogicalType& struct_type);
	static Value ExtractListFromNode(xmlNodePtr node, const LogicalType& list_type);
	static Value ExtractXMLArrayFromNode(xmlNodePtr node);
};

} // namespace duckdb