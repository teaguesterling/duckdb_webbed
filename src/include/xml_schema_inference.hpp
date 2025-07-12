#pragma once

#include "duckdb.hpp"
#include "xml_utils.hpp"
#include <libxml/parser.h>
#include <unordered_map>
#include <vector>

namespace duckdb {

// Configuration options for schema inference
struct XMLSchemaOptions {
	// Schema inference controls
	std::string root_element;           // Extract only children of specified root (empty = auto-detect)
	bool auto_detect = true;            // Automatic type detection
	int32_t schema_depth = 2;           // How deep to analyze (1-3)
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
	
	double GetFrequency(int32_t total_samples) const {
		return total_samples > 0 ? static_cast<double>(occurrence_count) / total_samples : 0.0;
	}
	
	bool IsListCandidate() const {
		return appears_in_array && has_homogeneous_structure;
	}
	
	bool IsStructCandidate() const {
		return has_children && !has_text && child_element_counts.size() > 0;
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
};

} // namespace duckdb