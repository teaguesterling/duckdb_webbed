#include "xml_schema_inference.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <regex>
#include <libxml/xpath.h>

namespace duckdb {

std::vector<XMLColumnInfo> XMLSchemaInference::InferSchema(const std::string& xml_content, 
                                                            const XMLSchemaOptions& options) {
	std::vector<XMLColumnInfo> columns;
	
	// First, analyze the document structure to detect patterns
	auto patterns = AnalyzeDocumentStructure(xml_content, options);
	
	if (patterns.empty()) {
		// Fallback: return filename and content columns
		columns.emplace_back("filename", LogicalType::VARCHAR, false, "", 1.0);
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
		return columns;
	}
	
	// Calculate total element occurrences for frequency calculation
	int32_t total_occurrences = 0;
	for (const auto& pattern : patterns) {
		total_occurrences += pattern.occurrence_count;
	}
	
	// Convert patterns to column definitions
	for (const auto& pattern : patterns) {
		// Skip elements that appear very rarely (likely outliers)
		if (total_occurrences > 0 && pattern.GetFrequency(total_occurrences) < 0.1) { // Less than 10% frequency
			continue;
		}
		
		// Create column for element text content (if it has text)
		if (pattern.has_text && !pattern.sample_values.empty()) {
			auto inferred_type = InferTypeFromSamples(pattern.sample_values, options);
			auto xpath = GetElementXPath(pattern.name);
			
			columns.emplace_back(pattern.name, inferred_type, false, xpath, 
			                     pattern.GetFrequency(total_occurrences));
		}
		
		// Create columns for attributes (if enabled)
		if (options.include_attributes && options.attribute_mode == "columns") {
			for (const auto& attr_pair : pattern.attribute_counts) {
				std::string attr_name = pattern.name + "_" + attr_pair.first;
				if (!options.attribute_prefix.empty()) {
					attr_name = options.attribute_prefix + attr_name;
				}
				
				// For now, assume VARCHAR for attributes (could be enhanced with sampling)
				auto xpath = GetElementXPath(pattern.name, true, attr_pair.first);
				columns.emplace_back(attr_name, LogicalType::VARCHAR, true, xpath, 0.8);
			}
		}
	}
	
	// Ensure we have at least some columns
	if (columns.empty()) {
		columns.emplace_back("filename", LogicalType::VARCHAR, false, "", 1.0);
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
	}
	
	return columns;
}

std::vector<ElementPattern> XMLSchemaInference::AnalyzeDocumentStructure(const std::string& xml_content,
                                                                          const XMLSchemaOptions& options) {
	XMLDocRAII xml_doc(xml_content);
	if (!xml_doc.IsValid()) {
		return {};
	}
	
	std::unordered_map<std::string, ElementPattern> pattern_map;
	
	// Find the root element or use specified root
	xmlNodePtr root = xmlDocGetRootElement(xml_doc.doc);
	if (!root) {
		return {};
	}
	
	// If a specific root element is specified, find it
	if (!options.root_element.empty()) {
		// Use XPath to find the specified root element
		std::string xpath = "//" + options.root_element;
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);
		
		if (xpath_obj && xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
			root = xpath_obj->nodesetval->nodeTab[0];
		}
		
		if (xpath_obj) xmlXPathFreeObject(xpath_obj);
	}
	
	// Analyze child elements of the root
	for (xmlNodePtr child = root->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			AnalyzeElement(child, pattern_map, options);
		}
	}
	
	// Convert map to vector and sort by frequency
	std::vector<ElementPattern> patterns;
	for (const auto& pair : pattern_map) {
		patterns.push_back(pair.second);
	}
	
	std::sort(patterns.begin(), patterns.end(), 
	          [](const ElementPattern& a, const ElementPattern& b) {
		          return a.occurrence_count > b.occurrence_count;
	          });
	
	return patterns;
}

void XMLSchemaInference::AnalyzeElement(xmlNodePtr node, std::unordered_map<std::string, ElementPattern>& patterns,
                                        const XMLSchemaOptions& options, int32_t current_depth) {
	if (!node || node->type != XML_ELEMENT_NODE || current_depth >= options.schema_depth) {
		return;
	}
	
	std::string element_name((const char*)node->name);
	auto& pattern = patterns[element_name];
	pattern.name = element_name;
	pattern.occurrence_count++;
	
	// Check for text content
	xmlChar* content = xmlNodeGetContent(node);
	if (content) {
		std::string text_content = CleanTextContent((const char*)content);
		if (!text_content.empty()) {
			pattern.has_text = true;
			if (pattern.sample_values.size() < 20) { // Limit sample size
				pattern.sample_values.push_back(text_content);
			}
		}
		xmlFree(content);
	}
	
	// Check for attributes
	for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
		if (attr->name) {
			std::string attr_name((const char*)attr->name);
			pattern.attribute_counts[attr_name]++;
		}
	}
	
	// Check for child elements
	bool has_element_children = false;
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			has_element_children = true;
			// Recursively analyze children (but limit depth)
			AnalyzeElement(child, patterns, options, current_depth + 1);
		}
	}
	pattern.has_children = has_element_children;
}

LogicalType XMLSchemaInference::InferTypeFromSamples(const std::vector<std::string>& samples,
                                                      const XMLSchemaOptions& options) {
	if (samples.empty()) {
		return LogicalType::VARCHAR;
	}
	
	std::vector<LogicalType> detected_types;
	
	for (const auto& sample : samples) {
		if (sample.empty()) {
			continue; // Skip empty values
		}
		
		// Test types in order of specificity
		if (options.boolean_detection && IsBoolean(sample)) {
			detected_types.push_back(LogicalType::BOOLEAN);
		} else if (options.numeric_detection && IsInteger(sample)) {
			detected_types.push_back(LogicalType::INTEGER);
		} else if (options.numeric_detection && IsDouble(sample)) {
			detected_types.push_back(LogicalType::DOUBLE);
		} else if (options.temporal_detection && IsDate(sample)) {
			detected_types.push_back(LogicalType::DATE);
		} else if (options.temporal_detection && IsTimestamp(sample)) {
			detected_types.push_back(LogicalType::TIMESTAMP);
		} else if (options.temporal_detection && IsTime(sample)) {
			detected_types.push_back(LogicalType::TIME);
		} else {
			detected_types.push_back(LogicalType::VARCHAR);
		}
	}
	
	return GetMostSpecificType(detected_types);
}

bool XMLSchemaInference::IsBoolean(const std::string& value) {
	std::string lower = StringUtil::Lower(value);
	return lower == "true" || lower == "false" || 
	       lower == "yes" || lower == "no" ||
	       lower == "1" || lower == "0" ||
	       lower == "on" || lower == "off";
}

bool XMLSchemaInference::IsInteger(const std::string& value) {
	if (value.empty()) return false;
	
	try {
		size_t pos;
		std::stoll(value, &pos);
		return pos == value.length(); // Entire string was converted
	} catch (...) {
		return false;
	}
}

bool XMLSchemaInference::IsDouble(const std::string& value) {
	if (value.empty()) return false;
	
	try {
		size_t pos;
		std::stod(value, &pos);
		return pos == value.length(); // Entire string was converted
	} catch (...) {
		return false;
	}
}

bool XMLSchemaInference::IsDate(const std::string& value) {
	// Match common date formats: YYYY-MM-DD, DD/MM/YYYY, MM/DD/YYYY, etc.
	std::regex date_patterns[] = {
		std::regex(R"(\d{4}-\d{2}-\d{2})"),     // YYYY-MM-DD
		std::regex(R"(\d{2}/\d{2}/\d{4})"),     // MM/DD/YYYY or DD/MM/YYYY
		std::regex(R"(\d{4}/\d{2}/\d{2})"),     // YYYY/MM/DD
		std::regex(R"(\d{2}-\d{2}-\d{4})"),     // MM-DD-YYYY or DD-MM-YYYY
	};
	
	for (const auto& pattern : date_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

bool XMLSchemaInference::IsTime(const std::string& value) {
	// Match time formats: HH:MM:SS, HH:MM, HH:MM:SS.sss
	std::regex time_patterns[] = {
		std::regex(R"(\d{2}:\d{2}:\d{2})"),           // HH:MM:SS
		std::regex(R"(\d{2}:\d{2})"),                 // HH:MM
		std::regex(R"(\d{2}:\d{2}:\d{2}\.\d+)"),      // HH:MM:SS.sss
	};
	
	for (const auto& pattern : time_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

bool XMLSchemaInference::IsTimestamp(const std::string& value) {
	// Match ISO timestamp formats and common variations
	std::regex timestamp_patterns[] = {
		std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})"),       // YYYY-MM-DDTHH:MM:SS
		std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"),       // YYYY-MM-DD HH:MM:SS
		std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+)"),  // YYYY-MM-DDTHH:MM:SS.sss
		std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"),      // YYYY-MM-DDTHH:MM:SSZ
	};
	
	for (const auto& pattern : timestamp_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

std::string XMLSchemaInference::GetElementXPath(const std::string& element_name, bool is_attribute,
                                                 const std::string& attribute_name) {
	if (is_attribute) {
		return "//" + element_name + "/@" + attribute_name;
	} else {
		return "//" + element_name;
	}
}

LogicalType XMLSchemaInference::GetMostSpecificType(const std::vector<LogicalType>& types) {
	if (types.empty()) {
		return LogicalType::VARCHAR;
	}
	
	// Count occurrences of each type
	std::unordered_map<LogicalTypeId, int> type_counts;
	for (const auto& type : types) {
		type_counts[type.id()]++;
	}
	
	// If more than 80% of values are the same type, use that type
	double threshold = 0.8;
	int total = types.size();
	
	for (const auto& pair : type_counts) {
		if (static_cast<double>(pair.second) / total >= threshold) {
			return LogicalType(pair.first);
		}
	}
	
	// Fallback: if we have mixed types, prefer VARCHAR for safety
	return LogicalType::VARCHAR;
}

std::string XMLSchemaInference::CleanTextContent(const std::string& text) {
	// Remove leading/trailing whitespace and normalize
	std::string cleaned = text;
	StringUtil::Trim(cleaned);
	
	// Remove excessive whitespace
	std::regex multiple_spaces(R"(\s+)");
	cleaned = std::regex_replace(cleaned, multiple_spaces, " ");
	
	return cleaned;
}

} // namespace duckdb