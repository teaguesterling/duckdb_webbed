#include "xml_utils.hpp"
#include "xml_types.hpp"
#include "duckdb/common/exception.hpp"
#include <libxml/xmlerror.h>
#include <libxml/xmlschemas.h>
#include <libxml/HTMLparser.h>
#include <iostream>
#include <functional>
#include <set>
#include <map>
#include <unordered_set>

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

// Silent error handler that suppresses libxml2 warnings during normal operations
void XMLSilentErrorHandler(void *ctx, const char *msg, ...) {
	// Silently capture errors without printing to stderr
	xml_parse_error_occurred = true;
}

// Silent error handler for schema validation (with varargs to match xmlSchemaValidityErrorFunc)
void XMLSilentSchemaErrorHandler(void *ctx, const char *msg, ...) {
	// Silently capture schema validation errors without printing to stderr
	xml_parse_error_occurred = true;
}

XMLDocRAII::XMLDocRAII(const std::string &xml_str) {
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

XMLDocRAII::XMLDocRAII(const std::string &content, bool is_html) {
	// Reset error state
	xml_parse_error_occurred = false;
	xml_parse_error_message.clear();

	// Set error handler
	xmlSetGenericErrorFunc(nullptr, XMLErrorHandler);

	if (is_html) {
		// Parse as HTML using libxml2's HTML parser
		doc = htmlReadMemory(content.c_str(), content.length(), nullptr, nullptr,
		                     HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
	} else {
		// Parse as XML (same as original constructor)
		xmlParserCtxtPtr parser_ctx = xmlNewParserCtxt();
		if (parser_ctx) {
			doc = xmlCtxtReadMemory(parser_ctx, content.c_str(), content.length(), nullptr, nullptr, 0);
			xmlFreeParserCtxt(parser_ctx);
		}
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

XMLDocRAII::XMLDocRAII(XMLDocRAII &&other) noexcept : doc(other.doc), xpath_ctx(other.xpath_ctx) {
	other.doc = nullptr;
	other.xpath_ctx = nullptr;
}

XMLDocRAII &XMLDocRAII::operator=(XMLDocRAII &&other) noexcept {
	if (this != &other) {
		// Clean up current resources
		if (xpath_ctx)
			xmlXPathFreeContext(xpath_ctx);
		if (doc)
			xmlFreeDoc(doc);

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

bool XMLUtils::IsValidXML(const std::string &xml_str) {
	XMLDocRAII xml_doc(xml_str);
	return xml_doc.IsValid() && !xml_parse_error_occurred;
}

bool XMLUtils::IsWellFormedXML(const std::string &xml_str) {
	return IsValidXML(xml_str);
}

std::string XMLUtils::GetNodePath(xmlNodePtr node) {
	if (!node)
		return "";

	std::vector<std::string> path_parts;
	xmlNodePtr current = node;

	while (current && current->type == XML_ELEMENT_NODE) {
		if (current->name) {
			path_parts.insert(path_parts.begin(), std::string((const char *)current->name));
		}
		current = current->parent;
	}

	std::string path = "/";
	for (const auto &part : path_parts) {
		path += part + "/";
	}
	return path.substr(0, path.length() - 1); // Remove trailing slash
}

XMLElement XMLUtils::ProcessXMLNode(xmlNodePtr node) {
	XMLElement element;

	if (!node)
		return element;

	// Handle text nodes differently
	if (node->type == XML_TEXT_NODE) {
		element.name = "#text";
		if (node->content) {
			element.text_content = std::string((const char *)node->content);
		}
		element.line_number = xmlGetLineNo(node);
		return element;
	}

	// Set name
	if (node->name) {
		element.name = std::string((const char *)node->name);
	}

	// Set text content (for element nodes, get direct text content only)
	if (node->type == XML_ELEMENT_NODE) {
		// Get only direct text children, not all descendants
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_TEXT_NODE && child->content) {
				element.text_content += std::string((const char *)child->content);
			}
		}
	} else {
		xmlChar *content = xmlNodeGetContent(node);
		if (content) {
			element.text_content = std::string((const char *)content);
			xmlFree(content);
		}
	}

	// Set attributes
	for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
		if (attr->name && attr->children && attr->children->content) {
			std::string attr_name((const char *)attr->name);
			std::string attr_value((const char *)attr->children->content);
			element.attributes[attr_name] = attr_value;
		}
	}

	// Set namespace URI
	if (node->ns && node->ns->href) {
		element.namespace_uri = std::string((const char *)node->ns->href);
	}

	// Set path
	element.path = GetNodePath(node);

	// Set line number
	element.line_number = xmlGetLineNo(node);

	return element;
}

std::vector<XMLElement> XMLUtils::ExtractByXPath(const std::string &xml_str, const std::string &xpath) {
	std::vector<XMLElement> results;

	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return results;
	}

	// Suppress XPath warnings (e.g., undefined namespace prefixes)
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);

	// Restore normal error handling
	xmlSetGenericErrorFunc(nullptr, nullptr);

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

std::string XMLUtils::ExtractTextByXPath(const std::string &xml_str, const std::string &xpath) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return "";
	}

	// Suppress XPath warnings (e.g., undefined namespace prefixes)
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);

	// Restore normal error handling
	xmlSetGenericErrorFunc(nullptr, nullptr);

	std::string result;
	if (xpath_obj) {
		if (xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
			xmlNodePtr node = xpath_obj->nodesetval->nodeTab[0];
			if (node) {
				xmlChar *content = xmlNodeGetContent(node);
				if (content) {
					result = std::string((const char *)content);
					xmlFree(content);
				}
			}
		}
		xmlXPathFreeObject(xpath_obj);
	}

	return result;
}

std::string XMLUtils::PrettyPrintXML(const std::string &xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return xml_str; // Return original if parsing fails
	}

	xmlChar *formatted_str = nullptr;
	int size = 0;
	xmlDocDumpFormatMemory(xml_doc.doc, &formatted_str, &size, 1); // format=1 for pretty printing

	XMLCharPtr formatted_ptr(formatted_str); // DuckDB-style smart pointer
	return formatted_ptr ? std::string(reinterpret_cast<const char *>(formatted_ptr.get())) : xml_str;
}

std::string XMLUtils::MinifyXML(const std::string &xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return xml_str; // Return original if parsing fails
	}

	xmlChar *minified_str = nullptr;
	int size = 0;
	xmlDocDumpMemory(xml_doc.doc, &minified_str, &size); // No formatting

	XMLCharPtr minified_ptr(minified_str); // DuckDB-style smart pointer
	return minified_ptr ? std::string(reinterpret_cast<const char *>(minified_ptr.get())) : xml_str;
}

bool XMLUtils::ValidateXMLSchema(const std::string &xml_str, const std::string &xsd_schema) {
	// Suppress schema validation warnings
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

	// Parse the XSD schema using DuckDB-style smart pointers
	XMLSchemaParserPtr parser_ctx(xmlSchemaNewMemParserCtxt(xsd_schema.c_str(), xsd_schema.length()));
	if (!parser_ctx) {
		xmlSetGenericErrorFunc(nullptr, nullptr);
		return false;
	}

	// Set silent error handler for schema parsing
	xmlSchemaSetParserErrors(parser_ctx.get(), XMLSilentSchemaErrorHandler, XMLSilentSchemaErrorHandler, nullptr);

	XMLSchemaPtr schema(xmlSchemaParse(parser_ctx.get()));
	if (!schema) {
		xmlSetGenericErrorFunc(nullptr, nullptr);
		return false;
	}

	// Create validation context
	XMLSchemaValidPtr valid_ctx(xmlSchemaNewValidCtxt(schema.get()));
	if (!valid_ctx) {
		xmlSetGenericErrorFunc(nullptr, nullptr);
		return false;
	}

	// Set silent error handler for validation
	xmlSchemaSetValidErrors(valid_ctx.get(), XMLSilentSchemaErrorHandler, XMLSilentSchemaErrorHandler, nullptr);

	// Parse and validate the XML document
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		xmlSetGenericErrorFunc(nullptr, nullptr);
		return false;
	}

	int validation_result = xmlSchemaValidateDoc(valid_ctx.get(), xml_doc.doc);

	// Restore normal error handling
	xmlSetGenericErrorFunc(nullptr, nullptr);

	return (validation_result == 0);
}

std::vector<XMLComment> XMLUtils::ExtractComments(const std::string &xml_str) {
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
					comment.content = std::string((const char *)cur->content);
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

std::vector<XMLComment> XMLUtils::ExtractCData(const std::string &xml_str) {
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
					cdata.content = std::string((const char *)cur->content);
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

std::vector<XMLNamespace> XMLUtils::ExtractNamespaces(const std::string &xml_str) {
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
				std::string prefix = cur->ns->prefix ? std::string((const char *)cur->ns->prefix) : "";
				std::string uri = std::string((const char *)cur->ns->href);

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
					std::string prefix = ns->prefix ? std::string((const char *)ns->prefix) : "";
					std::string uri = std::string((const char *)ns->href);

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

XMLStats XMLUtils::GetXMLStats(const std::string &xml_str) {
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
					unique_namespaces.insert(std::string((const char *)cur->ns->href));
				}

				// Traverse element children only for depth calculation
				if (cur->children) {
					traverse_stats(cur->children, depth + 1);
				}
			}
		}
	};

	traverse_stats(xmlDocGetRootElement(xml_doc.doc), 1);
	stats.namespace_count = unique_namespaces.size();

	return stats;
}

std::string XMLUtils::XMLToJSON(const std::string &xml_str) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return "{}";
	}

	std::function<std::string(xmlNodePtr, bool)> node_to_json = [&](xmlNodePtr node, bool is_root) -> std::string {
		if (!node || node->type != XML_ELEMENT_NODE) {
			return "null";
		}

		std::string node_name = std::string((const char *)node->name);
		std::string result = "{";

		// Add attributes if any
		bool has_content = false;
		for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
			if (has_content)
				result += ",";
			std::string attr_name = std::string((const char *)attr->name);

			XMLCharPtr attr_value(xmlNodeListGetString(xml_doc.doc, attr->children, 1)); // DuckDB-style smart pointer
			std::string attr_val = attr_value ? std::string(reinterpret_cast<const char *>(attr_value.get())) : "";

			result += "\"@" + attr_name + "\":\"" + attr_val + "\"";
			has_content = true;
		}

		// Get direct text content only (not including children text)
		std::string direct_text;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_TEXT_NODE && child->content) {
				direct_text += std::string((const char *)child->content);
			}
		}

		// Clean up whitespace
		direct_text.erase(0, direct_text.find_first_not_of(" \t\n\r"));
		direct_text.erase(direct_text.find_last_not_of(" \t\n\r") + 1);

		if (!direct_text.empty()) {
			if (has_content)
				result += ",";
			result += "\"#text\":\"" + direct_text + "\"";
			has_content = true;
		}

		// Add children elements
		std::map<std::string, std::vector<std::string>> children_by_name;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				std::string child_name = std::string((const char *)child->name);
				std::string child_json = node_to_json(child, false);
				children_by_name[child_name].push_back(child_json);
			}
		}

		for (const auto &child_group : children_by_name) {
			if (has_content)
				result += ",";

			if (child_group.second.size() == 1) {
				result += "\"" + child_group.first + "\":" + child_group.second[0];
			} else {
				result += "\"" + child_group.first + "\":[";
				for (size_t i = 0; i < child_group.second.size(); i++) {
					if (i > 0)
						result += ",";
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

std::string XMLUtils::XMLToJSON(const std::string &xml_str, const XMLToJSONOptions &options) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid()) {
		return "{}";
	}

	// Create a set for fast lookup of force_list elements
	std::unordered_set<std::string> force_list_set(options.force_list.begin(), options.force_list.end());

	std::function<std::string(xmlNodePtr, bool)> node_to_json = [&](xmlNodePtr node, bool is_root) -> std::string {
		if (!node || node->type != XML_ELEMENT_NODE) {
			return "null";
		}

		// Get node name based on namespace handling mode
		std::string node_name;
		std::string local_name = std::string((const char *)node->name);

		if (options.namespaces == "strip") {
			// Strip namespaces: use local name only
			node_name = local_name;
		} else if (options.namespaces == "expand" && node->ns && node->ns->href) {
			// Expand: use full URI as prefix
			node_name = std::string((const char *)node->ns->href) + ":" + local_name;
		} else if (options.namespaces == "keep" && node->ns && node->ns->prefix) {
			// Keep: preserve namespace prefix
			node_name = std::string((const char *)node->ns->prefix) + ":" + local_name;
		} else {
			// No namespace or keep mode without prefix
			node_name = local_name;
		}

		std::string result = "{";

		// Add attributes if any
		bool has_content = false;
		for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
			if (has_content)
				result += ",";

			std::string attr_local_name = std::string((const char *)attr->name);
			std::string attr_name;

			if (options.namespaces == "strip") {
				// Strip namespaces from attributes
				attr_name = attr_local_name;
			} else if (options.namespaces == "expand" && attr->ns && attr->ns->href) {
				// Expand: use full URI as prefix
				attr_name = std::string((const char *)attr->ns->href) + ":" + attr_local_name;
			} else if (options.namespaces == "keep" && attr->ns && attr->ns->prefix) {
				// Keep: preserve namespace prefix
				attr_name = std::string((const char *)attr->ns->prefix) + ":" + attr_local_name;
			} else {
				// No namespace or keep mode without prefix
				attr_name = attr_local_name;
			}

			XMLCharPtr attr_value(xmlNodeListGetString(xml_doc.doc, attr->children, 1));
			std::string attr_val = attr_value ? std::string(reinterpret_cast<const char *>(attr_value.get())) : "";

			result += "\"" + options.attr_prefix + attr_name + "\":\"" + attr_val + "\"";
			has_content = true;
		}

		// Add namespace declarations if xmlns_key is set and this is the root
		if (is_root && !options.xmlns_key.empty()) {
			// Collect all namespace declarations
			std::map<std::string, std::string> namespaces;

			// Get all namespace definitions on this node
			for (xmlNsPtr ns = node->nsDef; ns; ns = ns->next) {
				std::string prefix = ns->prefix ? std::string((const char *)ns->prefix) : "";
				std::string href = ns->href ? std::string((const char *)ns->href) : "";
				if (!href.empty()) {
					namespaces[prefix] = href;
				}
			}

			// Add xmlns metadata if any namespaces found
			if (!namespaces.empty()) {
				if (has_content)
					result += ",";
				result += "\"" + options.xmlns_key + "\":{";
				bool first_ns = true;
				for (const auto &ns_pair : namespaces) {
					if (!first_ns)
						result += ",";
					result += "\"" + ns_pair.first + "\":\"" + ns_pair.second + "\"";
					first_ns = false;
				}
				result += "}";
				has_content = true;
			}
		}

		// Get direct text content only (not including children text)
		std::string direct_text;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_TEXT_NODE && child->content) {
				direct_text += std::string((const char *)child->content);
			}
		}

		// Clean up whitespace
		direct_text.erase(0, direct_text.find_first_not_of(" \t\n\r"));
		direct_text.erase(direct_text.find_last_not_of(" \t\n\r") + 1);

		if (!direct_text.empty()) {
			if (has_content)
				result += ",";
			result += "\"" + options.text_key + "\":\"" + direct_text + "\"";
			has_content = true;
		}

		// Add children elements
		std::map<std::string, std::vector<std::string>> children_by_name;
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				// Get child name using same namespace logic as parent
				std::string child_local_name = std::string((const char *)child->name);
				std::string child_name;

				if (options.namespaces == "strip") {
					child_name = child_local_name;
				} else if (options.namespaces == "expand" && child->ns && child->ns->href) {
					child_name = std::string((const char *)child->ns->href) + ":" + child_local_name;
				} else if (options.namespaces == "keep" && child->ns && child->ns->prefix) {
					child_name = std::string((const char *)child->ns->prefix) + ":" + child_local_name;
				} else {
					child_name = child_local_name;
				}

				std::string child_json = node_to_json(child, false);
				children_by_name[child_name].push_back(child_json);
			}
		}

		for (const auto &child_group : children_by_name) {
			if (has_content)
				result += ",";

			// Check if this element should be forced to a list OR has multiple instances
			bool should_be_array = force_list_set.count(child_group.first) > 0 || child_group.second.size() > 1;

			if (should_be_array) {
				result += "\"" + child_group.first + "\":[";
				for (size_t i = 0; i < child_group.second.size(); i++) {
					if (i > 0)
						result += ",";
					result += child_group.second[i];
				}
				result += "]";
			} else {
				result += "\"" + child_group.first + "\":" + child_group.second[0];
			}
			has_content = true;
		}

		// Handle empty elements if no content was added
		if (!has_content) {
			if (options.empty_elements == "null") {
				return "null";
			} else if (options.empty_elements == "string") {
				return "\"\"";
			}
			// Default "object" case - return empty object
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

std::string XMLUtils::JSONToXML(const std::string &json_str) {
	if (json_str.empty() || json_str == "{}") {
		return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root></root>";
	}

	// Create a new XML document
	XMLDocPtr doc(xmlNewDoc(BAD_CAST "1.0"));
	if (!doc) {
		return "<?xml version=\"1.0\"?>\n<root></root>\n";
	}

	// Simple JSON parser implementation
	// This handles basic JSON structures: objects, arrays, strings, numbers, booleans, null
	std::function<xmlNodePtr(const std::string &, const std::string &, xmlDocPtr)> json_to_node =
	    [&](const std::string &json_value, const std::string &node_name, xmlDocPtr document) -> xmlNodePtr {
		std::string trimmed = json_value;
		// Trim whitespace
		trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
		trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

		xmlNodePtr node = xmlNewNode(nullptr, BAD_CAST node_name.c_str());
		if (!node)
			return nullptr;

		if (trimmed.empty() || trimmed == "null") {
			// Empty node for null values
			return node;
		}

		if (trimmed[0] == '"' && trimmed[trimmed.length() - 1] == '"') {
			// String value - remove quotes and set as text content
			std::string str_content = trimmed.substr(1, trimmed.length() - 2);
			xmlNodePtr text_node = xmlNewText(BAD_CAST str_content.c_str());
			if (text_node) {
				xmlAddChild(node, text_node);
			}
			return node;
		}

		if (trimmed[0] == '[' && trimmed[trimmed.length() - 1] == ']') {
			// Array - each element becomes a child with same name
			std::string array_content = trimmed.substr(1, trimmed.length() - 2);

			// Simple array parsing
			std::vector<std::string> elements;
			int brace_depth = 0;
			int bracket_depth = 0;
			bool in_string = false;
			size_t element_start = 0;

			for (size_t i = 0; i < array_content.length(); i++) {
				char c = array_content[i];

				if (!in_string) {
					if (c == '"')
						in_string = true;
					else if (c == '{')
						brace_depth++;
					else if (c == '}')
						brace_depth--;
					else if (c == '[')
						bracket_depth++;
					else if (c == ']')
						bracket_depth--;
					else if (c == ',' && brace_depth == 0 && bracket_depth == 0) {
						elements.push_back(array_content.substr(element_start, i - element_start));
						element_start = i + 1;
					}
				} else {
					if (c == '"' && (i == 0 || array_content[i - 1] != '\\')) {
						in_string = false;
					}
				}
			}

			// Add the last element
			if (element_start < array_content.length()) {
				elements.push_back(array_content.substr(element_start));
			}

			// Convert each array element to a child node
			for (const auto &element : elements) {
				xmlNodePtr child = json_to_node(element, node_name, document);
				if (child) {
					xmlAddChild(node, child);
				}
			}

			// Change parent node name to indicate it's a list
			xmlNodeSetName(node, BAD_CAST(node_name + "_list").c_str());
			return node;
		}

		if (trimmed[0] == '{' && trimmed[trimmed.length() - 1] == '}') {
			// Object - each property becomes a child element
			std::string object_content = trimmed.substr(1, trimmed.length() - 2);

			// Simple object parsing
			std::map<std::string, std::string> properties;
			int brace_depth = 0;
			int bracket_depth = 0;
			bool in_string = false;
			size_t prop_start = 0;

			for (size_t i = 0; i < object_content.length(); i++) {
				char c = object_content[i];

				if (!in_string) {
					if (c == '"')
						in_string = true;
					else if (c == '{')
						brace_depth++;
					else if (c == '}')
						brace_depth--;
					else if (c == '[')
						bracket_depth++;
					else if (c == ']')
						bracket_depth--;
					else if (c == ',' && brace_depth == 0 && bracket_depth == 0) {
						std::string prop = object_content.substr(prop_start, i - prop_start);

						// Parse key:value pair
						size_t colon_pos = prop.find(':');
						if (colon_pos != std::string::npos) {
							std::string key = prop.substr(0, colon_pos);
							std::string value = prop.substr(colon_pos + 1);

							// Trim and remove quotes from key
							key.erase(0, key.find_first_not_of(" \t\n\r"));
							key.erase(key.find_last_not_of(" \t\n\r") + 1);
							if (key.length() >= 2 && key[0] == '"' && key[key.length() - 1] == '"') {
								key = key.substr(1, key.length() - 2);
							}

							// Trim value
							value.erase(0, value.find_first_not_of(" \t\n\r"));
							value.erase(value.find_last_not_of(" \t\n\r") + 1);

							properties[key] = value;
						}

						prop_start = i + 1;
					}
				} else {
					if (c == '"' && (i == 0 || object_content[i - 1] != '\\')) {
						in_string = false;
					}
				}
			}

			// Add the last property
			if (prop_start < object_content.length()) {
				std::string prop = object_content.substr(prop_start);

				size_t colon_pos = prop.find(':');
				if (colon_pos != std::string::npos) {
					std::string key = prop.substr(0, colon_pos);
					std::string value = prop.substr(colon_pos + 1);

					// Trim and remove quotes from key
					key.erase(0, key.find_first_not_of(" \t\n\r"));
					key.erase(key.find_last_not_of(" \t\n\r") + 1);
					if (key.length() >= 2 && key[0] == '"' && key[key.length() - 1] == '"') {
						key = key.substr(1, key.length() - 2);
					}

					// Trim value
					value.erase(0, value.find_first_not_of(" \t\n\r"));
					value.erase(value.find_last_not_of(" \t\n\r") + 1);

					properties[key] = value;
				}
			}

			// Handle properties - separate attributes from elements
			std::string text_content;

			for (const auto &prop : properties) {
				if (prop.first.length() > 0 && prop.first[0] == '@') {
					// This is an attribute (starts with @)
					std::string attr_name = prop.first.substr(1); // Remove @ prefix
					std::string attr_value = prop.second;

					// Remove quotes from attribute value if present
					if (attr_value.length() >= 2 && attr_value[0] == '"' &&
					    attr_value[attr_value.length() - 1] == '"') {
						attr_value = attr_value.substr(1, attr_value.length() - 2);
					}

					xmlSetProp(node, BAD_CAST attr_name.c_str(), BAD_CAST attr_value.c_str());
				} else if (prop.first == "#text") {
					// This is text content
					text_content = prop.second;

					// Remove quotes from text content if present
					if (text_content.length() >= 2 && text_content[0] == '"' &&
					    text_content[text_content.length() - 1] == '"') {
						text_content = text_content.substr(1, text_content.length() - 2);
					}
				} else {
					// This is a child element
					xmlNodePtr child = json_to_node(prop.second, prop.first, document);
					if (child) {
						xmlAddChild(node, child);
					}
				}
			}

			// Add text content if present
			if (!text_content.empty()) {
				xmlNodePtr text_node = xmlNewText(BAD_CAST text_content.c_str());
				if (text_node) {
					xmlAddChild(node, text_node);
				}
			}

			return node;
		}

		// Primitive value (number, boolean) - set as text content
		xmlNodePtr text_node = xmlNewText(BAD_CAST trimmed.c_str());
		if (text_node) {
			xmlAddChild(node, text_node);
		}

		return node;
	};

	// Check if JSON is an object with a single key that could be the root element name
	std::string trimmed = json_str;
	trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
	trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

	std::string root_element_name = "root";
	std::string actual_json_content = json_str;

	// Check if JSON has the pattern {"root_name": {...}} and extract it
	if (trimmed.length() > 4 && trimmed[0] == '{' && trimmed[trimmed.length() - 1] == '}') {
		// Simple parsing to find first key
		size_t first_quote = trimmed.find('"', 1);
		if (first_quote != std::string::npos) {
			size_t second_quote = trimmed.find('"', first_quote + 1);
			if (second_quote != std::string::npos) {
				size_t colon = trimmed.find(':', second_quote);
				if (colon != std::string::npos) {
					std::string potential_root = trimmed.substr(first_quote + 1, second_quote - first_quote - 1);

					// Check if the value is an object (indicating this should be the root element)
					size_t value_start = colon + 1;
					while (value_start < trimmed.length() &&
					       (trimmed[value_start] == ' ' || trimmed[value_start] == '\t')) {
						value_start++;
					}

					if (value_start < trimmed.length() && trimmed[value_start] == '{') {
						// Find the matching closing brace
						int brace_count = 1;
						size_t value_end = value_start + 1;
						bool in_string = false;

						while (value_end < trimmed.length() && brace_count > 0) {
							char c = trimmed[value_end];
							if (!in_string) {
								if (c == '"')
									in_string = true;
								else if (c == '{')
									brace_count++;
								else if (c == '}')
									brace_count--;
							} else {
								if (c == '"' && (value_end == 0 || trimmed[value_end - 1] != '\\')) {
									in_string = false;
								}
							}
							value_end++;
						}

						// Check if this is the only key-value pair in the object
						size_t remaining_start = value_end;
						while (remaining_start < trimmed.length() &&
						       (trimmed[remaining_start] == ' ' || trimmed[remaining_start] == '\t')) {
							remaining_start++;
						}

						if (remaining_start < trimmed.length() && trimmed[remaining_start] == '}') {
							// This is the only key, use it as root element and extract its value
							root_element_name = potential_root;
							actual_json_content = trimmed.substr(value_start, value_end - value_start);
						}
					}
				}
			}
		}
	}

	// Convert JSON to XML node
	xmlNodePtr root_node = json_to_node(actual_json_content, root_element_name, doc.get());
	if (root_node) {
		xmlDocSetRootElement(doc.get(), root_node);
	}

	// Convert to string with encoding="UTF-8"
	xmlChar *xml_string = nullptr;
	int size = 0;
	xmlDocDumpFormatMemoryEnc(doc.get(), &xml_string, &size, "UTF-8", 0);

	XMLCharPtr xml_ptr(xml_string);
	std::string result = xml_ptr ? std::string(reinterpret_cast<const char *>(xml_ptr.get()))
	                             : "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root></root>";

	// Remove trailing newline to match expected test format
	if (!result.empty() && result.back() == '\n') {
		result.pop_back();
	}

	return result;
}

std::string XMLUtils::ExtractXMLFragment(const std::string &xml_str, const std::string &xpath) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return "";
	}

	// Suppress XPath warnings (e.g., undefined namespace prefixes)
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);

	// Restore normal error handling
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return "";
	}

	std::string fragment_xml;

	// Return only the first matching node
	if (xpath_obj->nodesetval->nodeNr > 0) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[0];
		if (node) {
			// Create a temporary document for dumping this node
			XMLDocPtr temp_doc(xmlNewDoc(BAD_CAST "1.0"));
			if (temp_doc) {
				// Copy the node to avoid ownership issues
				xmlNodePtr copied_node = xmlCopyNode(node, 1); // 1 = recursive copy
				if (copied_node) {
					xmlDocSetRootElement(temp_doc.get(), copied_node);

					// Dump the node as XML string
					xmlChar *node_str = nullptr;
					int size = 0;
					xmlDocDumpMemory(temp_doc.get(), &node_str, &size);

					if (node_str) {
						XMLCharPtr node_ptr(node_str);
						std::string node_xml = std::string(reinterpret_cast<const char *>(node_ptr.get()));

						// Remove XML declaration from individual nodes
						size_t xml_decl_end = node_xml.find("?>");
						if (xml_decl_end != std::string::npos) {
							node_xml = node_xml.substr(xml_decl_end + 2);
							// Remove leading whitespace/newlines
							node_xml.erase(0, node_xml.find_first_not_of(" \t\n\r"));
						}

						// Remove trailing whitespace/newlines
						size_t end = node_xml.find_last_not_of(" \t\n\r");
						if (end != std::string::npos) {
							node_xml = node_xml.substr(0, end + 1);
						}

						fragment_xml = node_xml;
					}
				}
			}
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return fragment_xml;
}

std::string XMLUtils::ExtractXMLFragmentAll(const std::string &xml_str, const std::string &xpath) {
	XMLDocRAII xml_doc(xml_str);
	if (!xml_doc.IsValid() || !xml_doc.xpath_ctx) {
		return "";
	}

	// Suppress XPath warnings (e.g., undefined namespace prefixes)
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);

	// Restore normal error handling
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return "";
	}

	std::string fragment_xml;

	// Return ALL matching nodes, separated by newlines
	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
		if (node) {
			// Create a temporary document for dumping this node
			XMLDocPtr temp_doc(xmlNewDoc(BAD_CAST "1.0"));
			if (temp_doc) {
				// Copy the node to avoid ownership issues
				xmlNodePtr copied_node = xmlCopyNode(node, 1); // 1 = recursive copy
				if (copied_node) {
					xmlDocSetRootElement(temp_doc.get(), copied_node);

					// Dump the node as XML string
					xmlChar *node_str = nullptr;
					int size = 0;
					xmlDocDumpMemory(temp_doc.get(), &node_str, &size);

					if (node_str) {
						XMLCharPtr node_ptr(node_str);
						std::string node_xml = std::string(reinterpret_cast<const char *>(node_ptr.get()));

						// Remove XML declaration from individual nodes
						size_t xml_decl_end = node_xml.find("?>");
						if (xml_decl_end != std::string::npos) {
							node_xml = node_xml.substr(xml_decl_end + 2);
							// Remove leading whitespace/newlines
							node_xml.erase(0, node_xml.find_first_not_of(" \t\n\r"));
						}

						// Remove trailing whitespace/newlines
						size_t end = node_xml.find_last_not_of(" \t\n\r");
						if (end != std::string::npos) {
							node_xml = node_xml.substr(0, end + 1);
						}

						if (!fragment_xml.empty()) {
							fragment_xml += "\n";
						}
						fragment_xml += node_xml;
					}
				}
			}
		}
	}

	xmlXPathFreeObject(xpath_obj);

	// Add trailing newline for consistent string splitting
	if (!fragment_xml.empty()) {
		fragment_xml += "\n";
	}

	return fragment_xml;
}

std::string XMLUtils::ScalarToXML(const std::string &value, const std::string &node_name) {
	// Use RAII-safe libxml2 document creation
	XMLDocPtr doc(xmlNewDoc(BAD_CAST "1.0"));
	if (!doc) {
		return "<" + node_name + "></" + node_name + ">";
	}

	// Create root node
	xmlNodePtr root_node = xmlNewNode(nullptr, BAD_CAST node_name.c_str());
	if (!root_node) {
		return "<" + node_name + "></" + node_name + ">";
	}

	xmlDocSetRootElement(doc.get(), root_node);

	// Add text content (libxml2 handles XML escaping automatically)
	xmlNodePtr text_node = xmlNewText(BAD_CAST value.c_str());
	if (text_node) {
		xmlAddChild(root_node, text_node);
	}

	// Convert to string using RAII-safe memory management
	xmlChar *xml_string = nullptr;
	int size = 0;
	xmlDocDumpMemory(doc.get(), &xml_string, &size);

	// Use XMLCharPtr for automatic cleanup
	XMLCharPtr xml_ptr(xml_string);

	if (xml_ptr) {
		return std::string(reinterpret_cast<const char *>(xml_ptr.get()));
	} else {
		return "<" + node_name + ">" + value + "</" + node_name + ">";
	}
}

void XMLUtils::ConvertListToXML(Vector &input_vector, Vector &result, idx_t count, const std::string &node_name) {
	auto list_suffix = "_list";
	auto element_name = node_name;

	for (idx_t i = 0; i < count; i++) {
		auto list_value = FlatVector::GetData<list_entry_t>(input_vector)[i];
		auto &child_vector = ListVector::GetEntry(input_vector);
		auto child_type = ListType::GetChildType(input_vector.GetType());

		XMLDocPtr doc(xmlNewDoc(BAD_CAST "1.0"));
		if (!doc) {
			result.SetValue(i, Value("<" + node_name + list_suffix + "></" + node_name + list_suffix + ">"));
			continue;
		}

		// Create root container element
		xmlNodePtr root_node = xmlNewNode(nullptr, BAD_CAST(node_name + list_suffix).c_str());
		xmlDocSetRootElement(doc.get(), root_node);

		// Process each list element
		for (idx_t j = 0; j < list_value.length; j++) {
			idx_t child_idx = list_value.offset + j;

			// Create element node for each list item
			xmlNodePtr item_node = xmlNewNode(nullptr, BAD_CAST element_name.c_str());
			xmlAddChild(root_node, item_node);

			// Convert the child value based on its type
			if (child_type.id() == LogicalTypeId::VARCHAR) {
				auto child_data = FlatVector::GetData<string_t>(child_vector);
				std::string child_str = child_data[child_idx].GetString();
				xmlNodePtr text_node = xmlNewText(BAD_CAST child_str.c_str());
				if (text_node)
					xmlAddChild(item_node, text_node);
			} else {
				// For other types, convert recursively
				Value child_value = child_vector.GetValue(child_idx);
				xmlNodePtr child_node = ConvertValueToXMLNode(child_value, child_type, element_name, doc.get());
				if (child_node) {
					// Add the child node's children to the item node
					xmlNodePtr content = child_node->children;
					while (content) {
						xmlNodePtr next = content->next;
						xmlUnlinkNode(content);
						xmlAddChild(item_node, content);
						content = next;
					}
					// Free the wrapper node
					xmlFreeNode(child_node);
				}
			}
		}

		// Convert document to string
		xmlChar *xml_string = nullptr;
		int size = 0;
		xmlDocDumpMemory(doc.get(), &xml_string, &size);

		XMLCharPtr xml_ptr(xml_string);
		std::string xml_result = xml_ptr ? std::string(reinterpret_cast<const char *>(xml_ptr.get()))
		                                 : "<" + node_name + list_suffix + "></" + node_name + list_suffix + ">";

		result.SetValue(i, Value(xml_result));
	}
}

void XMLUtils::ConvertStructToXML(Vector &input_vector, Vector &result, idx_t count, const std::string &node_name) {
	auto struct_type = input_vector.GetType();
	auto &child_types = StructType::GetChildTypes(struct_type);

	for (idx_t i = 0; i < count; i++) {
		XMLDocPtr doc(xmlNewDoc(BAD_CAST "1.0"));
		if (!doc) {
			result.SetValue(i, Value("<" + node_name + "></" + node_name + ">"));
			continue;
		}

		// Create root element
		xmlNodePtr root_node = xmlNewNode(nullptr, BAD_CAST node_name.c_str());
		xmlDocSetRootElement(doc.get(), root_node);

		// Process each struct field
		for (idx_t field_idx = 0; field_idx < child_types.size(); field_idx++) {
			auto &field_name = child_types[field_idx].first;
			auto &field_type = child_types[field_idx].second;

			// Get the child vector for this field
			auto &field_vector = StructVector::GetEntries(input_vector)[field_idx];

			// Create field element
			xmlNodePtr field_node = xmlNewNode(nullptr, BAD_CAST field_name.c_str());
			xmlAddChild(root_node, field_node);

			// Get field value and convert recursively using new node-based function
			Value field_value = field_vector->GetValue(i);
			if (!field_value.IsNull()) {
				xmlNodePtr child_node = ConvertValueToXMLNode(field_value, field_type, field_name, doc.get());
				if (child_node) {
					// Add the child node directly to the field node
					// Note: we need to add the *children* of the returned node, not the node itself
					// since ConvertValueToXMLNode already creates the named node
					xmlNodePtr content = child_node->children;
					while (content) {
						xmlNodePtr next = content->next;
						xmlUnlinkNode(content);
						xmlAddChild(field_node, content);
						content = next;
					}
					// Free the wrapper node
					xmlFreeNode(child_node);
				}
			} else {
				// For NULL values, field_node remains empty
			}
		}

		// Convert document to string
		xmlChar *xml_string = nullptr;
		int size = 0;
		xmlDocDumpMemory(doc.get(), &xml_string, &size);

		XMLCharPtr xml_ptr(xml_string);
		std::string xml_result = xml_ptr ? std::string(reinterpret_cast<const char *>(xml_ptr.get()))
		                                 : "<" + node_name + "></" + node_name + ">";

		result.SetValue(i, Value(xml_result));
	}
}

xmlNodePtr XMLUtils::ConvertValueToXMLNode(const Value &value, const LogicalType &type, const std::string &node_name,
                                           xmlDocPtr doc) {
	if (!doc) {
		return nullptr;
	}

	// Create the main node for this value
	xmlNodePtr node = xmlNewNode(nullptr, BAD_CAST node_name.c_str());
	if (!node) {
		return nullptr;
	}

	if (value.IsNull()) {
		// Empty node for NULL values
		return node;
	}

	// Handle type hierarchy similar to ValueToXMLFunction
	if (XMLTypes::IsXMLFragmentType(type)) {
		// XMLFragment → Insert verbatim as text
		std::string content = value.GetValue<string>();
		xmlNodePtr text_node = xmlNewText(BAD_CAST content.c_str());
		if (text_node)
			xmlAddChild(node, text_node);
		return node;
	} else if (XMLTypes::IsXMLType(type)) {
		// XML → Insert verbatim as text
		std::string content = value.GetValue<string>();
		xmlNodePtr text_node = xmlNewText(BAD_CAST content.c_str());
		if (text_node)
			xmlAddChild(node, text_node);
		return node;
	} else if (type.id() == LogicalTypeId::LIST) {
		// LIST → Create list structure with _list suffix
		// Change node name to include _list suffix
		xmlNodeSetName(node, BAD_CAST(node_name + "_list").c_str());

		auto child_type = ListType::GetChildType(type);
		auto list_value = ListValue::GetChildren(value);

		// Add each list element as a child node
		for (const auto &child_value : list_value) {
			xmlNodePtr child_node = ConvertValueToXMLNode(child_value, child_type, node_name, doc);
			if (child_node) {
				xmlAddChild(node, child_node);
			}
		}
		return node;
	} else if (type.id() == LogicalTypeId::STRUCT) {
		// STRUCT → Create struct with field names as tags
		auto struct_value = StructValue::GetChildren(value);
		auto &child_types = StructType::GetChildTypes(type);

		// Add each struct field as a child node
		for (size_t i = 0; i < child_types.size(); i++) {
			auto &field_name = child_types[i].first;
			auto &field_type = child_types[i].second;
			auto &field_value = struct_value[i];

			xmlNodePtr field_node = ConvertValueToXMLNode(field_value, field_type, field_name, doc);
			if (field_node) {
				xmlAddChild(node, field_node);
			}
		}
		return node;
	} else {
		// Check if this is an explicit JSON type (has JSON alias)
		bool is_json_type = (type.id() == LogicalTypeId::VARCHAR && type.HasAlias() && type.GetAlias() == "JSON");

		if (is_json_type) {
			// JSON → Structural conversion
			// For now, treat as text - could be enhanced to parse JSON and create nodes
			std::string json_str = value.GetValue<string>();
			std::string xml_result = JSONToXML(json_str);

			// Parse the XML result and add child nodes
			xmlDocPtr temp_doc = xmlParseMemory(xml_result.c_str(), xml_result.length());
			if (temp_doc && xmlDocGetRootElement(temp_doc)) {
				xmlNodePtr temp_root = xmlDocGetRootElement(temp_doc);

				// Copy children from temp root to our node
				xmlNodePtr child = temp_root->children;
				while (child) {
					xmlNodePtr next = child->next;
					xmlUnlinkNode(child);
					xmlAddChild(node, child);
					child = next;
				}
				xmlFreeDoc(temp_doc);
			} else {
				// Fallback to text
				xmlNodePtr text_node = xmlNewText(BAD_CAST json_str.c_str());
				if (text_node)
					xmlAddChild(node, text_node);
			}
			return node;
		} else {
			// VARCHAR/Other → String content
			std::string value_str;
			if (type.id() == LogicalTypeId::VARCHAR) {
				value_str = value.GetValue<string>();
			} else {
				value_str = value.ToString();
			}

			// Check if input is already valid XML
			if (type.id() == LogicalTypeId::VARCHAR && IsValidXML(value_str)) {
				// Parse and add as child nodes
				xmlDocPtr temp_doc = xmlParseMemory(value_str.c_str(), value_str.length());
				if (temp_doc && xmlDocGetRootElement(temp_doc)) {
					xmlNodePtr temp_root = xmlDocGetRootElement(temp_doc);
					xmlNodePtr copied_node = xmlCopyNode(temp_root, 1);
					if (copied_node) {
						xmlAddChild(node, copied_node);
					}
					xmlFreeDoc(temp_doc);
				} else {
					// Fallback to text
					xmlNodePtr text_node = xmlNewText(BAD_CAST value_str.c_str());
					if (text_node)
						xmlAddChild(node, text_node);
				}
			} else {
				// Simple text content
				xmlNodePtr text_node = xmlNewText(BAD_CAST value_str.c_str());
				if (text_node)
					xmlAddChild(node, text_node);
			}
			return node;
		}
	}
}

// HTML-specific extraction functions
std::vector<HTMLLink> XMLUtils::ExtractHTMLLinks(const std::string &html_str) {
	std::vector<HTMLLink> links;

	XMLDocRAII html_doc(html_str, true); // Use HTML parser
	if (!html_doc.IsValid() || !html_doc.xpath_ctx) {
		return links;
	}

	// Find all <a> elements with href attributes
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST "//a[@href]", html_doc.xpath_ctx);
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return links;
	}

	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
		if (node && node->type == XML_ELEMENT_NODE) {
			HTMLLink link;

			// Get href attribute using RAII
			XMLCharPtr href(xmlGetProp(node, BAD_CAST "href"));
			if (href) {
				link.url = std::string(reinterpret_cast<const char *>(href.get()));
			}

			// Get title attribute using RAII
			XMLCharPtr title(xmlGetProp(node, BAD_CAST "title"));
			if (title) {
				link.title = std::string(reinterpret_cast<const char *>(title.get()));
			}

			// Get text content using RAII
			XMLCharPtr text(xmlNodeGetContent(node));
			if (text) {
				link.text = std::string(reinterpret_cast<const char *>(text.get()));
			}

			link.line_number = node->line;
			links.push_back(link);
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return links;
}

std::vector<HTMLImage> XMLUtils::ExtractHTMLImages(const std::string &html_str) {
	std::vector<HTMLImage> images;

	XMLDocRAII html_doc(html_str, true); // Use HTML parser
	if (!html_doc.IsValid() || !html_doc.xpath_ctx) {
		return images;
	}

	// Find all <img> elements
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST "//img", html_doc.xpath_ctx);
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return images;
	}

	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
		if (node && node->type == XML_ELEMENT_NODE) {
			HTMLImage image;

			// Get src attribute using RAII
			XMLCharPtr src(xmlGetProp(node, BAD_CAST "src"));
			if (src) {
				image.src = std::string(reinterpret_cast<const char *>(src.get()));
			}

			// Get alt attribute using RAII
			XMLCharPtr alt(xmlGetProp(node, BAD_CAST "alt"));
			if (alt) {
				image.alt_text = std::string(reinterpret_cast<const char *>(alt.get()));
			}

			// Get title attribute using RAII
			XMLCharPtr title(xmlGetProp(node, BAD_CAST "title"));
			if (title) {
				image.title = std::string(reinterpret_cast<const char *>(title.get()));
			}

			// Get width attribute using RAII
			XMLCharPtr width(xmlGetProp(node, BAD_CAST "width"));
			if (width) {
				try {
					image.width = std::stoll(std::string(reinterpret_cast<const char *>(width.get())));
				} catch (...) {
					image.width = 0;
				}
			}

			// Get height attribute using RAII
			XMLCharPtr height(xmlGetProp(node, BAD_CAST "height"));
			if (height) {
				try {
					image.height = std::stoll(std::string(reinterpret_cast<const char *>(height.get())));
				} catch (...) {
					image.height = 0;
				}
			}

			image.line_number = node->line;
			images.push_back(image);
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return images;
}

std::vector<HTMLTable> XMLUtils::ExtractHTMLTables(const std::string &html_str) {
	std::vector<HTMLTable> tables;

	XMLDocRAII html_doc(html_str, true); // Use HTML parser
	if (!html_doc.IsValid() || !html_doc.xpath_ctx) {
		return tables;
	}

	// Find all <table> elements
	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST "//table", html_doc.xpath_ctx);
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return tables;
	}

	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		xmlNodePtr table_node = xpath_obj->nodesetval->nodeTab[i];
		if (table_node && table_node->type == XML_ELEMENT_NODE) {
			HTMLTable table;
			table.line_number = table_node->line;

			// Extract header row from thead/th or first tr/th
			xmlXPathContextPtr local_ctx = xmlXPathNewContext(html_doc.doc);
			if (local_ctx) {
				// Set context to this table node
				local_ctx->node = table_node;

				// Look for header cells (th elements)
				xmlXPathObjectPtr header_obj =
				    xmlXPathEvalExpression(BAD_CAST ".//thead//th | .//tr[1]//th", local_ctx);

				if (header_obj && header_obj->nodesetval && header_obj->nodesetval->nodeNr > 0) {
					// Found th elements - use as headers
					for (int j = 0; j < header_obj->nodesetval->nodeNr; j++) {
						xmlNodePtr th_node = header_obj->nodesetval->nodeTab[j];
						XMLCharPtr text(xmlNodeGetContent(th_node));
						if (text) {
							table.headers.push_back(std::string(reinterpret_cast<const char *>(text.get())));
						} else {
							table.headers.push_back("");
						}
					}
				}

				if (header_obj)
					xmlXPathFreeObject(header_obj);

				// Extract data rows (all rows for tables without th headers)
				bool has_th_headers = !table.headers.empty();
				std::string data_xpath = has_th_headers ? ".//tbody//tr | .//tr[not(th)]" : ".//tbody//tr | .//tr";

				xmlXPathObjectPtr rows_obj = xmlXPathEvalExpression(BAD_CAST data_xpath.c_str(), local_ctx);

				if (rows_obj && rows_obj->nodesetval) {
					for (int j = 0; j < rows_obj->nodesetval->nodeNr; j++) {
						xmlNodePtr row_node = rows_obj->nodesetval->nodeTab[j];
						std::vector<std::string> row_data;

						// Extract td elements from this row
						xmlXPathContextPtr row_ctx = xmlXPathNewContext(html_doc.doc);
						if (row_ctx) {
							row_ctx->node = row_node;
							xmlXPathObjectPtr cells_obj = xmlXPathEvalExpression(BAD_CAST ".//td", row_ctx);

							if (cells_obj && cells_obj->nodesetval) {
								for (int k = 0; k < cells_obj->nodesetval->nodeNr; k++) {
									xmlNodePtr cell_node = cells_obj->nodesetval->nodeTab[k];
									XMLCharPtr text(xmlNodeGetContent(cell_node));
									if (text) {
										row_data.push_back(std::string(reinterpret_cast<const char *>(text.get())));
									} else {
										row_data.push_back("");
									}
								}
							}

							if (cells_obj)
								xmlXPathFreeObject(cells_obj);
							xmlXPathFreeContext(row_ctx);
						}

						if (!row_data.empty()) {
							table.rows.push_back(row_data);
						}
					}
				}

				if (rows_obj)
					xmlXPathFreeObject(rows_obj);
				xmlXPathFreeContext(local_ctx);
			}

			// Set table metadata
			table.num_columns = static_cast<int64_t>(table.headers.size());
			table.num_rows = static_cast<int64_t>(table.rows.size());

			// Only add table if it has content
			if (table.num_columns > 0 || table.num_rows > 0) {
				tables.push_back(table);
			}
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return tables;
}

std::string XMLUtils::ExtractHTMLText(const std::string &html_str, const std::string &selector) {
	XMLDocRAII html_doc(html_str, true); // Use HTML parser
	if (!html_doc.IsValid() || !html_doc.xpath_ctx) {
		return "";
	}

	std::string xpath = selector.empty() ? "//text()" : selector;

	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), html_doc.xpath_ctx);
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return "";
	}

	std::string text_content;
	for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[i];
		if (node) {
			XMLCharPtr content(xmlNodeGetContent(node));
			if (content) {
				text_content += std::string(reinterpret_cast<const char *>(content.get()));
			}
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return text_content;
}

std::string XMLUtils::ExtractHTMLTextByXPath(const std::string &html_str, const std::string &xpath) {
	XMLDocRAII html_doc(html_str, true); // Use HTML parser
	if (!html_doc.IsValid() || !html_doc.xpath_ctx) {
		return "";
	}

	xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
	xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), html_doc.xpath_ctx);
	xmlSetGenericErrorFunc(nullptr, nullptr);

	if (!xpath_obj || !xpath_obj->nodesetval) {
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
		return "";
	}

	std::string text_content;
	// Return only the first match
	if (xpath_obj->nodesetval->nodeNr > 0) {
		xmlNodePtr node = xpath_obj->nodesetval->nodeTab[0];
		if (node) {
			XMLCharPtr content(xmlNodeGetContent(node));
			if (content) {
				text_content = std::string(reinterpret_cast<const char *>(content.get()));
			}
		}
	}

	xmlXPathFreeObject(xpath_obj);
	return text_content;
}

} // namespace duckdb
