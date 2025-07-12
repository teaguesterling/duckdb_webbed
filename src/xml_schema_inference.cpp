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

std::vector<std::vector<Value>> XMLSchemaInference::ExtractData(const std::string& xml_content,
                                                                const XMLSchemaOptions& options) {
	std::vector<std::vector<Value>> rows;
	
	// First, infer the schema to know what columns we need to extract
	auto schema = InferSchema(xml_content, options);
	
	if (schema.empty()) {
		return rows; // No data to extract
	}
	
	// Parse XML document
	XMLDocRAII doc(xml_content);
	if (!doc.IsValid()) {
		return rows; // Invalid XML
	}
	
	xmlNodePtr root = xmlDocGetRootElement(doc.doc);
	if (!root) {
		return rows; // No root element
	}
	
	// Find the repeating element pattern (e.g., "employee" elements in the employees.xml)
	// For now, assume the first child elements are the records
	xmlNodePtr current = root->children;
	
	while (current) {
		if (current->type == XML_ELEMENT_NODE) {
			// Extract data for this record
			std::vector<Value> row;
			
			for (const auto& column : schema) {
				Value value;
				
				if (column.is_attribute) {
					// Extract attribute value
					std::string attr_name = column.name;
					// Remove element prefix if present (e.g., "employee_id" -> "id")
					size_t underscore_pos = attr_name.find('_');
					if (underscore_pos != std::string::npos) {
						attr_name = attr_name.substr(underscore_pos + 1);
					}
					
					xmlChar* attr_value = xmlGetProp(current, (const xmlChar*)attr_name.c_str());
					if (attr_value) {
						std::string str_value = (const char*)attr_value;
						value = ConvertToValue(str_value, column.type);
						xmlFree(attr_value);
					} else {
						value = Value(); // NULL
					}
				} else {
					// Extract element text content
					xmlNodePtr child = current->children;
					std::string element_text;
					
					while (child) {
						if (child->type == XML_ELEMENT_NODE && 
						    xmlStrcmp(child->name, (const xmlChar*)column.name.c_str()) == 0) {
							
							// Check if this element has child elements (container) or just text
							bool has_element_children = false;
							for (xmlNodePtr grandchild = child->children; grandchild; grandchild = grandchild->next) {
								if (grandchild->type == XML_ELEMENT_NODE) {
									has_element_children = true;
									break;
								}
							}
							
							if (has_element_children) {
								// Container element - return as XML
								xmlBufferPtr buffer = xmlBufferCreate();
								if (buffer) {
									xmlNodeDump(buffer, child->doc, child, 0, 1);
									element_text = (const char*)xmlBufferContent(buffer);
									xmlBufferFree(buffer);
								}
							} else {
								// Leaf element - return text content
								xmlChar* text_content = xmlNodeGetContent(child);
								if (text_content) {
									element_text = CleanTextContent((const char*)text_content);
									xmlFree(text_content);
								}
							}
							break;
						}
						child = child->next;
					}
					
					value = ConvertToValue(element_text, column.type);
				}
				
				row.push_back(value);
			}
			
			rows.push_back(row);
		}
		current = current->next;
	}
	
	return rows;
}

std::vector<std::vector<Value>> XMLSchemaInference::ExtractDataWithSchema(const std::string& xml_content,
                                                                           const std::vector<std::string>& column_names,
                                                                           const std::vector<LogicalType>& column_types,
                                                                           const XMLSchemaOptions& options) {
	std::vector<std::vector<Value>> rows;
	
	if (column_names.size() != column_types.size()) {
		return rows; // Mismatch in schema specification
	}
	
	if (column_names.empty()) {
		return rows; // No columns to extract
	}
	
	// Parse XML document
	XMLDocRAII doc(xml_content);
	if (!doc.IsValid()) {
		return rows; // Invalid XML
	}
	
	xmlNodePtr root = xmlDocGetRootElement(doc.doc);
	if (!root) {
		return rows; // No root element
	}
	
	// Find the repeating element pattern (e.g., "employee" elements)
	// For now, assume the first child elements are the records
	xmlNodePtr current = root->children;
	
	while (current) {
		if (current->type == XML_ELEMENT_NODE) {
			// Extract data for this record according to explicit schema
			std::vector<Value> row;
			
			for (size_t col_idx = 0; col_idx < column_names.size(); col_idx++) {
				const auto& column_name = column_names[col_idx];
				const auto& column_type = column_types[col_idx];
				
				// Find the specific child element or attribute for this column
				Value value;
				
				// First check if it's an attribute
				xmlChar* attr_value = xmlGetProp(current, (const xmlChar*)column_name.c_str());
				if (attr_value) {
					std::string str_value = (const char*)attr_value;
					value = ConvertToValue(str_value, column_type);
					xmlFree(attr_value);
				} else {
					// Look for child element with matching name
					xmlNodePtr child = current->children;
					while (child) {
						if (child->type == XML_ELEMENT_NODE && 
						    xmlStrcmp(child->name, (const xmlChar*)column_name.c_str()) == 0) {
							// Found matching child element - extract recursively
							value = ExtractValueFromNode(child, column_type);
							break;
						}
						child = child->next;
					}
					
					// If no matching child found, use NULL
					if (child == nullptr) {
						value = Value(column_type);
					}
				}
				
				row.push_back(value);
			}
			
			rows.push_back(row);
		}
		current = current->next;
	}
	
	return rows;
}

Value XMLSchemaInference::ConvertToValue(const std::string& text, const LogicalType& target_type) {
	if (text.empty()) {
		return Value(); // NULL value
	}
	
	try {
		switch (target_type.id()) {
			case LogicalTypeId::BOOLEAN: {
				std::string lower = text;
				std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
				if (lower == "true" || lower == "yes" || lower == "1" || lower == "on") {
					return Value::BOOLEAN(true);
				} else if (lower == "false" || lower == "no" || lower == "0" || lower == "off") {
					return Value::BOOLEAN(false);
				}
				return Value(); // NULL for unrecognized boolean
			}
			case LogicalTypeId::INTEGER:
				return Value::INTEGER(std::stoi(text));
			case LogicalTypeId::BIGINT:
				return Value::BIGINT(std::stoll(text));
			case LogicalTypeId::DOUBLE:
				return Value::DOUBLE(std::stod(text));
			case LogicalTypeId::DATE: {
				// Try to parse date - simplified for now
				if (text.length() == 10 && text[4] == '-' && text[7] == '-') {
					return Value::DATE(Date::FromString(text));
				}
				return Value(text); // Fallback to string
			}
			case LogicalTypeId::TIMESTAMP: {
				// Try to parse timestamp
				return Value::TIMESTAMP(Timestamp::FromString(text));
			}
			case LogicalTypeId::VARCHAR:
			default:
				return Value(text);
		}
	} catch (...) {
		// If conversion fails, return as VARCHAR
		return Value(text);
	}
}

Value XMLSchemaInference::ExtractValueFromNode(xmlNodePtr node, const LogicalType& target_type) {
	if (!node) {
		return Value(); // NULL value
	}
	
	switch (target_type.id()) {
		case LogicalTypeId::LIST:
			return ExtractListFromNode(node, target_type);
		case LogicalTypeId::STRUCT:
			return ExtractStructFromNode(node, target_type);
		default: {
			// For primitive types, extract text content and convert
			xmlChar* text_content = xmlNodeGetContent(node);
			if (text_content) {
				std::string text = CleanTextContent((const char*)text_content);
				xmlFree(text_content);
				return ConvertToValue(text, target_type);
			}
			return Value(); // NULL value
		}
	}
}

Value XMLSchemaInference::ExtractStructFromNode(xmlNodePtr node, const LogicalType& struct_type) {
	if (!node || struct_type.id() != LogicalTypeId::STRUCT) {
		return Value(); // NULL value
	}
	
	auto &struct_children = StructType::GetChildTypes(struct_type);
	child_list_t<Value> struct_values;
	
	// Extract each field of the struct
	for (const auto& field : struct_children) {
		const auto& field_name = field.first;
		const auto& field_type = field.second;
		
		// Find child element with matching name
		xmlNodePtr child = node->children;
		Value field_value;
		
		while (child) {
			if (child->type == XML_ELEMENT_NODE && 
			    xmlStrcmp(child->name, (const xmlChar*)field_name.c_str()) == 0) {
				// Found matching child element - extract recursively
				field_value = ExtractValueFromNode(child, field_type);
				break;
			}
			child = child->next;
		}
		
		// If field not found, use NULL
		if (child == nullptr) {
			field_value = Value(field_type);
		}
		
		struct_values.push_back(make_pair(field_name, field_value));
	}
	
	return Value::STRUCT(struct_values);
}

Value XMLSchemaInference::ExtractListFromNode(xmlNodePtr node, const LogicalType& list_type) {
	if (!node || list_type.id() != LogicalTypeId::LIST) {
		return Value(); // NULL value
	}
	
	auto element_type = ListType::GetChildType(list_type);
	vector<Value> list_values;
	
	// Collect all child elements of the same type
	xmlNodePtr child = node->children;
	
	while (child) {
		if (child->type == XML_ELEMENT_NODE) {
			// Extract each child element according to the list element type
			Value element_value = ExtractValueFromNode(child, element_type);
			list_values.push_back(element_value);
		}
		child = child->next;
	}
	
	return Value::LIST(list_type, list_values);
}

} // namespace duckdb