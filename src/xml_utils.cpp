#include "xml_utils.hpp"
#include "duckdb/common/exception.hpp"
#include <libxml/xmlerror.h>
#include <libxml/xmlschemas.h>
#include <iostream>
#include <functional>
#include <set>
#include <map>

namespace duckdb {

// Global error handling for libxml2
static bool xml_parse_error_occurred = false;
static std::string xml_parse_error_message;

static void XMLErrorHandler(void *ctx, const char *msg, ...) {
	xml_parse_error_occurred = true;
	va_list args;
	va_start(args, msg);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), msg, args);
	va_end(args);
	xml_parse_error_message = std::string(buffer);
}

XMLDocRAII::XMLDocRAII(const std::string& xml_str) {
	// Reset error state
	xml_parse_error_occurred = false;
	xml_parse_error_message.clear();
	
	// Set error handler
	xmlSetGenericErrorFunc(nullptr, XMLErrorHandler);
	
	// Parse the XML with default options to preserve comments and CDATA
	xmlParserCtxtPtr parser_ctx = xmlNewParserCtxt();
	if (parser_ctx) {
		// Parse with default options (preserves comments and CDATA by default)
		doc = xmlCtxtReadMemory(parser_ctx, xml_str.c_str(), xml_str.length(), nullptr, nullptr, 0);
		xmlFreeParserCtxt(parser_ctx);
	}
	
	if (doc && !xml_parse_error_occurred) {
		xpath_ctx = xmlXPathNewContext(doc);
	}
	
	// Reset error handler
	xmlSetGenericErrorFunc(nullptr, nullptr);
}

XMLDocRAII::~XMLDocRAII() {
	if (xpath_ctx) {
		xmlXPathFreeContext(xpath_ctx);
		xpath_ctx = nullptr;
	}
	if (doc) {
		xmlFreeDoc(doc);
		doc = nullptr;
	}
}

XMLDocRAII::XMLDocRAII(XMLDocRAII&& other) noexcept 
	: doc(other.doc), xpath_ctx(other.xpath_ctx) {
	other.doc = nullptr;
	other.xpath_ctx = nullptr;
}

XMLDocRAII& XMLDocRAII::operator=(XMLDocRAII&& other) noexcept {
	if (this != &other) {
		// Clean up current resources
		if (xpath_ctx) xmlXPathFreeContext(xpath_ctx);
		if (doc) xmlFreeDoc(doc);
		
		// Move from other
		doc = other.doc;
		xpath_ctx = other.xpath_ctx;
		other.doc = nullptr;
		other.xpath_ctx = nullptr;
	}
	return *this;
}

void XMLUtils::InitializeLibXML() {
	xmlInitParser();
	LIBXML_TEST_VERSION;
}

void XMLUtils::CleanupLibXML() {
	xmlCleanupParser();
}

bool XMLUtils::IsValidXML(const std::string& xml_str) {
	XMLDocRAII xml_doc(xml_str);
	return xml_doc.IsValid() && !xml_parse_error_occurred;
}

bool XMLUtils::IsWellFormedXML(const std::string& xml_str) {
	return IsValidXML(xml_str);
}

std::string XMLUtils::GetNodePath(xmlNodePtr node) {
	if (!node) return "";
	
	std::vector<std::string> path_parts;
	xmlNodePtr current = node;
	
	while (current && current->type == XML_ELEMENT_NODE) {
		if (current->name) {
			path_parts.insert(path_parts.begin(), std::string((const char*)current->name));
		}
		current = current->parent;
	}
	
	std::string path = "/";
	for (const auto& part : path_parts) {
		path += part + "/";
	}
	return path.substr(0, path.length() - 1);  // Remove trailing slash
}

XMLElement XMLUtils::ProcessXMLNode(xmlNodePtr node) {
	XMLElement element;
	
	if (!node) return element;
	
	// Handle text nodes differently
	if (node->type == XML_TEXT_NODE) {
		element.name = "#text";
		if (node->content) {
			element.text_content = std::string((const char*)node->content);
		}
		element.line_number = xmlGetLineNo(node);
		return element;
	}
	
	// Set name
	if (node->name) {
		element.name = std::string((const char*)node->name);
	}
	
	// Set text content (for element nodes, get direct text content only)
	if (node->type == XML_ELEMENT_NODE) {
		// Get only direct text children, not all descendants
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_TEXT_NODE && child->content) {
				element.text_content += std::string((const char*)child->content);
			}
		}
	} else {
		xmlChar* content = xmlNodeGetContent(node);
		if (content) {
			element.text_content = std::string((const char*)content);
			xmlFree(content);
		}
	}
	
	// Set attributes
	for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
		if (attr->name && attr->children && attr->children->content) {
			std::string attr_name((const char*)attr->name);
			std::string attr_value((const char*)attr->children->content);
			element.attributes[attr_name] = attr_value;
		}
	}
	
	// Set namespace URI
	if (node->ns && node->ns->href) {
		element.namespace_uri = std::string((const char*)node->ns->href);
	}
	
	// Set path
	element.path = GetNodePath(node);
	
	// Set line number
	element.line_number = xmlGetLineNo(node);
	
	return element;
}

std::vector<XMLElement> XMLUtils::ExtractByXPath(const std::string& xml_str, const std::string& xpath) {
	std::vector<XMLElement> results;
	
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return results;
	}
	
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(
		BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);
	
	if (xpath_obj && xpath_obj->nodesetval) {
		for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
			xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
			if (node) {
				results.push_back(ProcessXMLNode(node));
			}
		}
	}
	
	if (xpath_obj) {
		xmlXPathFreeObject(xpath_obj);
	}
	
	return results;
}

std::string XMLUtils::ExtractTextByXPath(const std::string& xml_str, const std::string& xpath) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return "";
	}
	
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(
		BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);
	
	std::string result;
	if (xpath_obj) {
		if (xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
			xmlNodePtr node = xpath_obj->nodesetval->nodeTab[0];
			if (node) {
				xmlChar* content = xmlNodeGetContent(node);
				if (content) {
					result = std::string((const char*)content);
					xmlFree(content);
				}
			}
		}
		xmlXPathFreeObject(xpath_obj);
	}
	
	return result;
}

std::string XMLUtils::PrettyPrintXML(const std::string& xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return xml_str; // Return original if parsing fails
	}
	
	xmlChar* formatted_str = nullptr;
	int size = 0;
	xmlDocDumpFormatMemory(xml_doc.doc, &formatted_str, &size, 1); // format=1 for pretty printing
	
	XMLCharPtr formatted_ptr(formatted_str); // DuckDB-style smart pointer
	return formatted_ptr ? std::string(reinterpret_cast<const char*>(formatted_ptr.get())) : xml_str;
}

std::string XMLUtils::MinifyXML(const std::string& xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return xml_str; // Return original if parsing fails
	}
	
	xmlChar* minified_str = nullptr;
	int size = 0;
	xmlDocDumpMemory(xml_doc.doc, &minified_str, &size); // No formatting
	
	XMLCharPtr minified_ptr(minified_str); // DuckDB-style smart pointer
	return minified_ptr ? std::string(reinterpret_cast<const char*>(minified_ptr.get())) : xml_str;
}

bool XMLUtils::ValidateXMLSchema(const std::string& xml_str, const std::string& xsd_schema) {
	// Parse the XSD schema using DuckDB-style smart pointers
	XMLSchemaParserPtr parser_ctx(xmlSchemaNewMemParserCtxt(xsd_schema.c_str(), xsd_schema.length()));
	if (!parser_ctx) {
		return false;
	}
	
	XMLSchemaPtr schema(xmlSchemaParse(parser_ctx.get()));
	if (!schema) {
		return false;
	}
	
	// Create validation context
	XMLSchemaValidPtr valid_ctx(xmlSchemaNewValidCtxt(schema.get()));
	if (!valid_ctx) {
		return false;
	}
	
	// Parse and validate the XML document
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return false;
	}
	
	int validation_result = xmlSchemaValidateDoc(valid_ctx.get(), xml_doc.doc);
	return (validation_result == 0);
}

std::vector<XMLComment> XMLUtils::ExtractComments(const std::string& xml_str) {
	std::vector<XMLComment> comments;
	XMLDocRAII xml_doc(xml_str);
	
	if (!xml_doc.IsValid()) {
		return comments;
	}
	
	std::function<void(xmlNodePtr)> traverse_comments = [&](xmlNodePtr node) {
		for (xmlNodePtr cur = node; cur; cur = cur->next) {
			if (cur->type == XML_COMMENT_NODE) {
				XMLComment comment;
				if (cur->content) {
					comment.content = std::string((const char*)cur->content);
				}
				comment.line_number = xmlGetLineNo(cur);
				comments.push_back(comment);
			}
			
			// Traverse children
			if (cur->children) {
				traverse_comments(cur->children);
			}
		}
	};
	
	// Start from the document children to catch comments at document level
	xmlNodePtr doc_children = xml_doc.doc->children;
	if (doc_children) {
		traverse_comments(doc_children);
	}
	
	return comments;
}

std::vector<XMLComment> XMLUtils::ExtractCData(const std::string& xml_str) {
	std::vector<XMLComment> cdata_sections;
	XMLDocRAII xml_doc(xml_str);
	
	if (!xml_doc.IsValid()) {
		return cdata_sections;
	}
	
	std::function<void(xmlNodePtr)> traverse_cdata = [&](xmlNodePtr node) {
		for (xmlNodePtr cur = node; cur; cur = cur->next) {
			if (cur->type == XML_CDATA_SECTION_NODE) {
				XMLComment cdata;
				if (cur->content) {
					cdata.content = std::string((const char*)cur->content);
				}
				cdata.line_number = xmlGetLineNo(cur);
				cdata_sections.push_back(cdata);
			}
			
			// Traverse children
			if (cur->children) {
				traverse_cdata(cur->children);
			}
		}
	};
	
	// Start from the document children to catch CDATA at all levels
	xmlNodePtr doc_children = xml_doc.doc->children;
	if (doc_children) {
		traverse_cdata(doc_children);
	}
	
	return cdata_sections;
}

std::vector<XMLNamespace> XMLUtils::ExtractNamespaces(const std::string& xml_str) {
	std::vector<XMLNamespace> namespaces;
	XMLDocRAII xml_doc(xml_str);
	
	if (!xml_doc.IsValid()) {
		return namespaces;
	}
	
	std::set<std::pair<std::string, std::string>> seen_namespaces;
	
	std::function<void(xmlNodePtr)> traverse_namespaces = [&](xmlNodePtr node) {
		for (xmlNodePtr cur = node; cur; cur = cur->next) {
			// Check node's namespace
			if (cur->ns && cur->ns->href) {
				std::string prefix = cur->ns->prefix ? std::string((const char*)cur->ns->prefix) : "";
				std::string uri = std::string((const char*)cur->ns->href);
				
				if (seen_namespaces.find({prefix, uri}) == seen_namespaces.end()) {
					seen_namespaces.insert({prefix, uri});
					XMLNamespace ns;
					ns.prefix = prefix;
					ns.uri = uri;
					namespaces.push_back(ns);
				}
			}
			
			// Check namespace declarations
			for (xmlNsPtr ns = cur->nsDef; ns; ns = ns->next) {
				if (ns->href) {
					std::string prefix = ns->prefix ? std::string((const char*)ns->prefix) : "";
					std::string uri = std::string((const char*)ns->href);
					
					if (seen_namespaces.find({prefix, uri}) == seen_namespaces.end()) {
						seen_namespaces.insert({prefix, uri});
						XMLNamespace namespace_decl;
						namespace_decl.prefix = prefix;
						namespace_decl.uri = uri;
						namespaces.push_back(namespace_decl);
					}
				}
			}
			
			// Traverse children
			if (cur->children) {
				traverse_namespaces(cur->children);
			}
		}
	};
	
	traverse_namespaces(xmlDocGetRootElement(xml_doc.doc));
	return namespaces;
}

XMLStats XMLUtils::GetXMLStats(const std::string& xml_str) {
	XMLStats stats = {0, 0, 0, 0, 0};
	XMLDocRAII xml_doc(xml_str);
	
	if (!xml_doc.IsValid()) {
		return stats;
	}
	
	stats.size_bytes = xml_str.length();
	std::set<std::string> unique_namespaces;
	
	std::function<void(xmlNodePtr, int)> traverse_stats = [&](xmlNodePtr node, int depth) {
		stats.max_depth = std::max(stats.max_depth, (int64_t)depth);
		
		for (xmlNodePtr cur = node; cur; cur = cur->next) {
			if (cur->type == XML_ELEMENT_NODE) {
				stats.element_count++;
				
				// Count attributes
				for (xmlAttrPtr attr = cur->properties; attr; attr = attr->next) {
					stats.attribute_count++;
				}
				
				// Track namespaces
				if (cur->ns && cur->ns->href) {
					unique_namespaces.insert(std::string((const char*)cur->ns->href));
				}
			}
			
			// Traverse children
			if (cur->children) {
				traverse_stats(cur->children, depth + 1);
			}
		}
	};
	
	traverse_stats(xmlDocGetRootElement(xml_doc.doc), 1);
	stats.namespace_count = unique_namespaces.size();
	
	return stats;
}

std::string XMLUtils::XMLToJSON(const std::string& xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return "{}";
	}
	
	std::function<std::string(xmlNodePtr, bool)> node_to_json = [&](xmlNodePtr node, bool is_root) -> std::string {
		if (!node || node->type != XML_ELEMENT_NODE) {
			return "null";
		}
		
		std::string node_name = std::string((const char*)node->name);
		std::string result = "{";
		
		// Add attributes if any
		bool has_content = false;
		for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
			if (has_content) result += ",";
			std::string attr_name = std::string((const char*)attr->name);
			
			XMLCharPtr attr_value(xmlNodeListGetString(xml_doc.doc, attr->children, 1)); // DuckDB-style smart pointer
			std::string attr_val = attr_value ? std::string(reinterpret_cast<const char*>(attr_value.get())) : "";
			
			result += "\"@" + attr_name + "\":\"" + attr_val + "\"";
			has_content = true;
		}
		
		// Get direct text content only (not including children text)
		std::string direct_text;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_TEXT_NODE && child->content) {
				direct_text += std::string((const char*)child->content);
			}
		}
		
		// Clean up whitespace
		direct_text.erase(0, direct_text.find_first_not_of(" \t\n\r"));
		direct_text.erase(direct_text.find_last_not_of(" \t\n\r") + 1);
		
		if (!direct_text.empty()) {
			if (has_content) result += ",";
			result += "\"#text\":\"" + direct_text + "\"";
			has_content = true;
		}
		
		// Add children elements
		std::map<std::string, std::vector<std::string>> children_by_name;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				std::string child_name = std::string((const char*)child->name);
				std::string child_json = node_to_json(child, false);
				children_by_name[child_name].push_back(child_json);
			}
		}
		
		for (const auto& child_group : children_by_name) {
			if (has_content) result += ",";
			
			if (child_group.second.size() == 1) {
				result += "\"" + child_group.first + "\":" + child_group.second[0];
			} else {
				result += "\"" + child_group.first + "\":[";
				for (size_t i = 0; i < child_group.second.size(); i++) {
					if (i > 0) result += ",";
					result += child_group.second[i];
				}
				result += "]";
			}
			has_content = true;
		}
		
		result += "}";
		
		// For the root element, wrap it with the element name
		if (is_root) {
			return "{\"" + node_name + "\":" + result + "}";
		}
		
		return result;
	};
	
	xmlNodePtr root = xmlDocGetRootElement(xml_doc.doc);
	if (root) {
		return node_to_json(root, true);
	}
	
	return "{}";
}

std::string XMLUtils::JSONToXML(const std::string& json_str) {
	// For now, implement a simple JSON-to-XML conversion
	// This could be enhanced to use DuckDB's JSON functions in the future
	
	if (json_str.empty() || json_str == "{}") {
		return "<root></root>";
	}
	
	// Simple JSON-to-XML conversion logic
	// This is a basic implementation - could be enhanced with proper JSON parsing
	std::string result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	
	std::function<std::string(const std::string&, const std::string&)> convert_json_object = 
		[&](const std::string& json_content, const std::string& root_name) -> std::string {
		
		// Very basic JSON parsing - this should be enhanced with proper JSON library
		// For now, handle simple cases
		if (json_content.find("{") == 0 && json_content.rfind("}") == json_content.length() - 1) {
			std::string inner = json_content.substr(1, json_content.length() - 2);
			
			// Simple key-value parsing (basic implementation)
			std::string xml_content = "<" + root_name + ">";
			
			// This is a placeholder - in practice, we'd use DuckDB's JSON functions
			// or a proper JSON parser here
			size_t pos = 0;
			while (pos < inner.length()) {
				// Find key
				size_t key_start = inner.find("\"", pos);
				if (key_start == std::string::npos) break;
				
				size_t key_end = inner.find("\"", key_start + 1);
				if (key_end == std::string::npos) break;
				
				std::string key = inner.substr(key_start + 1, key_end - key_start - 1);
				
				// Find value
				size_t colon = inner.find(":", key_end);
				if (colon == std::string::npos) break;
				
				size_t value_start = colon + 1;
				while (value_start < inner.length() && (inner[value_start] == ' ' || inner[value_start] == '\t')) {
					value_start++;
				}
				
				size_t value_end = inner.find(",", value_start);
				if (value_end == std::string::npos) {
					value_end = inner.length();
				}
				
				std::string value = inner.substr(value_start, value_end - value_start);
				
				// Remove quotes from string values
				if (value.length() >= 2 && value[0] == '"' && value[value.length()-1] == '"') {
					value = value.substr(1, value.length() - 2);
				}
				
				// Handle special JSON keys
				if (key.substr(0, 1) == "@") {
					// This was an attribute in the original XML
					// For simplicity, convert back to attribute
					std::string attr_name = key.substr(1);
					// Note: This is simplified - proper implementation would handle attributes differently
					xml_content += "<" + attr_name + ">" + value + "</" + attr_name + ">";
				} else if (key == "#text") {
					// This was text content
					xml_content += value;
				} else {
					// Regular element
					xml_content += "<" + key + ">" + value + "</" + key + ">";
				}
				
				pos = value_end + 1;
			}
			
			xml_content += "</" + root_name + ">";
			return xml_content;
		}
		
		// Fallback for non-object JSON
		return "<" + root_name + ">" + json_content + "</" + root_name + ">";
	};
	
	// Try to extract root element name from JSON structure
	std::string root_name = "root";
	if (json_str.find("{\"") != std::string::npos) {
		size_t first_quote = json_str.find("\"");
		size_t second_quote = json_str.find("\"", first_quote + 1);
		if (first_quote != std::string::npos && second_quote != std::string::npos) {
			std::string potential_root = json_str.substr(first_quote + 1, second_quote - first_quote - 1);
			// If it looks like an element name, use it
			if (potential_root.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos) {
				root_name = potential_root;
				
				// Extract the inner object
				size_t colon = json_str.find(":", second_quote);
				if (colon != std::string::npos) {
					size_t value_start = colon + 1;
					while (value_start < json_str.length() && (json_str[value_start] == ' ' || json_str[value_start] == '\t')) {
						value_start++;
					}
					
					// Find matching brace
					if (value_start < json_str.length() && json_str[value_start] == '{') {
						int brace_count = 1;
						size_t value_end = value_start + 1;
						while (value_end < json_str.length() && brace_count > 0) {
							if (json_str[value_end] == '{') brace_count++;
							else if (json_str[value_end] == '}') brace_count--;
							value_end++;
						}
						
						if (brace_count == 0) {
							std::string inner_json = json_str.substr(value_start, value_end - value_start);
							result += convert_json_object(inner_json, root_name);
							return result;
						}
					}
				}
			}
		}
	}
	
	// Fallback: treat entire JSON as content of root element
	result += convert_json_object(json_str, root_name);
	return result;
}

} // namespace duckdb