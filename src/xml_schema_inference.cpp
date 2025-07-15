#include "xml_schema_inference.hpp"
#include "xml_types.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <regex>
#include <libxml/xpath.h>

namespace duckdb {

// Forward declaration for silent error handler from xml_utils.cpp
void XMLSilentErrorHandler(void *ctx, const char *msg, ...);

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
	
	// Create a map for pattern lookup during nested type inference
	std::unordered_map<std::string, ElementPattern> pattern_map;
	for (const auto& pattern : patterns) {
		pattern_map[pattern.name] = pattern;
	}
	
	// Convert patterns to column definitions
	for (const auto& pattern : patterns) {
		// Skip elements that appear very rarely (likely outliers)
		if (total_occurrences > 0 && pattern.GetFrequency(total_occurrences) < 0.1) { // Less than 10% frequency
			continue;
		}
		
		LogicalType column_type;
		bool should_create_column = false;
		
		// When unnest_as_columns=true, skip container elements that have children
		// Only create columns for leaf elements or properly aggregated arrays
		if (options.unnest_as_columns && pattern.has_children && !pattern.all_children_same_name) {
			// Before skipping container element, extract its attributes as columns
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
			// Skip this container element - its children will be processed as individual columns
			continue;
		}
		
		// Skip individual elements that appear in homogeneous arrays (they'll be aggregated)
		// Check if this element is a child of a homogeneous collection that will create a LIST column
		if (options.unnest_as_columns && pattern.is_scalar) {
			bool appears_in_homogeneous_collection = false;
			
			// Check if any other pattern contains this element as a homogeneous child collection
			for (const auto& other_pattern : patterns) {
				if (other_pattern.all_children_same_name && 
					other_pattern.child_element_counts.size() == 1 &&
					other_pattern.child_element_counts.find(pattern.name) != other_pattern.child_element_counts.end() &&
					other_pattern.child_element_counts.at(pattern.name) > 1) {
					// This scalar element appears multiple times under another element
					// The parent will create a LIST column, so skip this individual element
					appears_in_homogeneous_collection = true;
					break;
				}
			}
			
			if (appears_in_homogeneous_collection) {
				continue;
			}
		}
		
		// Determine the appropriate type using 4-tier priority system
		XMLTier tier = pattern.GetTier();
		
		switch (tier) {
			case XMLTier::HOMOGENEOUS_CONFORMING:
				// Can map to clean DuckDB types (SCALAR, LIST, STRUCT)
				if (pattern.is_scalar) {
					column_type = InferTypeFromSamples(pattern.sample_values, options);
				} else {
					column_type = InferNestedType(pattern, pattern_map, options);
				}
				should_create_column = true;
				break;
				
			case XMLTier::HETEROGENEOUS_CONFORMING:
				// Extractable but inconsistent structure - try nested types first
				column_type = InferNestedType(pattern, pattern_map, options);
				should_create_column = true;
				break;
				
			case XMLTier::EXTRACTABLE_AS_FRAGMENT:
				// Can unwrap as XMLFragment (content without parent wrapper)
				column_type = XMLTypes::XMLFragmentType();
				should_create_column = true;
				break;
				
			case XMLTier::FALLBACK_TO_XML:
			default:
				// Must preserve full XML context
				column_type = XMLTypes::XMLType();
				should_create_column = true;
				break;
		}
		
		if (should_create_column) {
			auto xpath = GetElementXPath(pattern.name);
			columns.emplace_back(pattern.name, column_type, false, xpath, 
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
		
		// Suppress XPath warnings (e.g., undefined namespace prefixes)
		xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
		
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);
		
		// Restore normal error handling
		xmlSetGenericErrorFunc(nullptr, nullptr);
		
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
	
	// Second pass: compute children_have_attributes and all_children_conforming
	for (auto& pattern_pair : pattern_map) {
		auto& pattern = pattern_pair.second;
		
		if (pattern.has_children) {
			pattern.children_have_attributes = false;
			pattern.all_children_conforming = true;
			
			// Check child attributes and consistency for proper tier detection
			XMLTier first_child_tier = XMLTier::FALLBACK_TO_XML; // Use as invalid sentinel
			bool children_have_consistent_tiers = true;
			bool first_child_set = false;
			
			for (const auto& child_name_count : pattern.child_element_counts) {
				const auto& child_name = child_name_count.first;
				auto child_iter = pattern_map.find(child_name);
				
				if (child_iter != pattern_map.end()) {
					const auto& child_pattern = child_iter->second;
					
					// Check if this child has attributes
					if (!child_pattern.attribute_counts.empty()) {
						pattern.children_have_attributes = true;
					}
					
					// Check consistency: for homogeneous lists, all children must have same tier
					XMLTier child_tier = child_pattern.GetTier();
					
					if (!first_child_set) {
						// First child - set the expected tier
						first_child_tier = child_tier;
						first_child_set = true;
					} else if (pattern.all_children_same_name && child_tier != first_child_tier) {
						// For LIST candidates, children must have identical tiers
						children_have_consistent_tiers = false;
					}
					// For STRUCT candidates, different tiers are OK as long as each is valid
				} else {
					// Child pattern not found - not conforming
					pattern.all_children_conforming = false;
					children_have_consistent_tiers = false;
				}
			}
			
			// Update conforming flag based on consistency check
			pattern.all_children_conforming = children_have_consistent_tiers;
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
	if (!node || node->type != XML_ELEMENT_NODE) {
		return;
	}
	
	// Optional depth limiting for performance (unlimited by default)
	if (options.max_depth >= 0 && current_depth >= options.max_depth) {
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
	
	// Check for child elements and analyze nested structure
	bool has_element_children = false;
	std::unordered_map<std::string, int32_t> child_counts;
	std::vector<std::string> child_names_ordered;
	
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			has_element_children = true;
			std::string child_name((const char*)child->name);
			
			// Track child element frequencies
			child_counts[child_name]++;
			pattern.child_element_counts[child_name]++;
			
			// Track order of first occurrence for STRUCT detection
			if (child_counts[child_name] == 1) {
				child_names_ordered.push_back(child_name);
			}
			
			// Recursively analyze children (but limit depth)
			AnalyzeElement(child, patterns, options, current_depth + 1);
		}
	}
	
	pattern.has_children = has_element_children;
	
	// Analyze nested structure patterns
	if (has_element_children) {
		// Check if this looks like an array container (repeated elements of same type)
		if (child_counts.size() == 1) {
			// Single type of child element repeated multiple times = this element contains a LIST
			auto child_name = child_counts.begin()->first;
			auto child_count = child_counts.begin()->second;
			if (child_count > 1) {
				// This element contains multiple instances of the same child = LIST container
				pattern.appears_in_array = true;
				pattern.has_homogeneous_structure = true;
			}
		} else if (child_counts.size() > 1) {
			// Multiple different child elements = potential STRUCT
			// Check if structure is consistent (all children appear only once)
			bool is_struct_like = true;
			for (const auto& child_pair : child_counts) {
				if (child_pair.second > 1) {
					is_struct_like = false;
					break;
				}
			}
			if (is_struct_like) {
				pattern.has_homogeneous_structure = true;
			}
		}
		
		// Store sample structure for consistency checking
		if (pattern.child_structures.size() < 5) { // Limit samples
			std::unordered_map<std::string, std::string> structure_sample;
			for (const auto& child_name : child_names_ordered) {
				structure_sample[child_name] = "element"; // Could enhance with type info
			}
			pattern.child_structures.push_back(structure_sample);
		}
	}
	
	// Compute flags for 6-tier priority system
	pattern.is_scalar = pattern.has_text && !pattern.has_children;
	pattern.has_attributes = !pattern.attribute_counts.empty();
	
	// Analyze child name patterns
	if (pattern.has_children) {
		pattern.all_children_same_name = (child_counts.size() == 1);
		
		// Check if all children have different names (no repeats)
		pattern.all_children_different_name = true;
		for (const auto& child_pair : child_counts) {
			if (child_pair.second > 1) {
				pattern.all_children_different_name = false;
				break;
			}
		}
		
		// We'll compute children_have_attributes and all_children_conforming 
		// in a second pass after all patterns are analyzed
	}
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

LogicalType XMLSchemaInference::InferNestedType(const ElementPattern& pattern,
                                                const std::unordered_map<std::string, ElementPattern>& all_patterns,
                                                const XMLSchemaOptions& options) {
	
	// If this element doesn't have children, it's not a nested type
	if (!pattern.has_children || pattern.child_element_counts.empty()) {
		return LogicalType::VARCHAR; // Fallback
	}
	
	XMLTier tier = pattern.GetTier();
	
	// Check if this is a homogeneous list (same-name children)
	if (pattern.all_children_same_name && pattern.child_element_counts.size() == 1) {
		// Homogeneous list - determine element type
		auto child_name = pattern.child_element_counts.begin()->first;
		auto child_pattern_iter = all_patterns.find(child_name);
		
		if (child_pattern_iter != all_patterns.end()) {
			const auto& child_pattern = child_pattern_iter->second;
			
			// Recursively determine child type
			LogicalType child_type;
			if (child_pattern.is_scalar) {
				child_type = InferTypeFromSamples(child_pattern.sample_values, options);
			} else {
				// Recursive nested type
				child_type = InferNestedType(child_pattern, all_patterns, options);
			}
			
			return LogicalType::LIST(child_type);
		}
	} else if (pattern.all_children_different_name) {
		// Heterogeneous struct - different-name children
		child_list_t<LogicalType> struct_fields;
		
		for (const auto& child_pair : pattern.child_element_counts) {
			const auto& child_name = child_pair.first;
			auto child_pattern_iter = all_patterns.find(child_name);
			
			if (child_pattern_iter != all_patterns.end()) {
				const auto& child_pattern = child_pattern_iter->second;
				
				// Recursively determine child type
				LogicalType child_type;
				if (child_pattern.is_scalar) {
					child_type = InferTypeFromSamples(child_pattern.sample_values, options);
				} else {
					// Recursive nested type
					child_type = InferNestedType(child_pattern, all_patterns, options);
				}
				
				struct_fields.push_back(make_pair(child_name, child_type));
			}
		}
		
		if (!struct_fields.empty()) {
			return LogicalType::STRUCT(struct_fields);
		}
	}
	
	// Fallback to VARCHAR (shouldn't happen with proper tier detection)
	return LogicalType::VARCHAR;
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
								// Container element - check for structured types first
								if (column.type.id() == LogicalTypeId::LIST || column.type.id() == LogicalTypeId::STRUCT) {
									// Use structured extraction for LIST and STRUCT types
									value = ExtractValueFromNode(child, column.type);
									element_text = ""; // Clear element_text to avoid double processing
									break;
								}
								
								// Check for XML[] type
								if (XMLTypes::IsXMLArrayType(column.type)) {
									value = ExtractXMLArrayFromNode(child);
									element_text = ""; // Clear element_text to avoid double processing
									break;
								}
								
								// Fall back to XML/XMLFragment format for unstructured types
								bool use_fragment = (column.type.HasAlias() && column.type.GetAlias() == "xmlfragment");
								
								if (use_fragment) {
									// XMLFragment: return unwrapped child elements
									xmlBufferPtr buffer = xmlBufferCreate();
									if (buffer) {
										for (xmlNodePtr grandchild = child->children; grandchild; grandchild = grandchild->next) {
											if (grandchild->type == XML_ELEMENT_NODE) {
												xmlNodeDump(buffer, grandchild->doc, grandchild, 0, 1);
											}
										}
										// RAII: Copy the content before freeing the buffer
										const xmlChar* content = xmlBufferContent(buffer);
										if (content) {
											element_text = std::string((const char*)content);
										}
										xmlBufferFree(buffer);
									}
								} else {
									// XML: return wrapped content (current behavior)
									xmlBufferPtr buffer = xmlBufferCreate();
									if (buffer) {
										xmlNodeDump(buffer, child->doc, child, 0, 1);
										// RAII: Copy the content before freeing the buffer
										const xmlChar* content = xmlBufferContent(buffer);
										if (content) {
											element_text = std::string((const char*)content);
										}
										xmlBufferFree(buffer);
									}
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
					
					// Only convert text value if we haven't already extracted a structured value
					if (value.IsNull()) {
						value = ConvertToValue(element_text, column.type);
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
	
	return Value::LIST(element_type, list_values);
}

Value XMLSchemaInference::ExtractXMLArrayFromNode(xmlNodePtr node) {
	if (!node) {
		return Value(); // NULL value
	}
	
	vector<Value> xml_values;
	xmlNodePtr child = node->children;
	
	while (child) {
		if (child->type == XML_ELEMENT_NODE) {
			// Each child element becomes a well-formed XML string
			xmlBufferPtr buffer = xmlBufferCreate();
			if (buffer) {
				xmlNodeDump(buffer, child->doc, child, 0, 1);
				
				// RAII: Copy the content before freeing the buffer
				const xmlChar* content = xmlBufferContent(buffer);
				if (content) {
					std::string xml_string((const char*)content);
					// Create XML-typed value
					Value xml_value(xml_string);
					xml_values.push_back(xml_value);
				}
				xmlBufferFree(buffer);
			}
		}
		child = child->next;
	}
	
	// Return as LIST<XML>
	return Value::LIST(XMLTypes::XMLType(), xml_values);
}

} // namespace duckdb