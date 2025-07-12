#include "xml_utils.hpp"
#include "duckdb/common/exception.hpp"
#include <libxml/xmlerror.h>
#include <iostream>

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
	
	// Parse the XML
	doc = xmlParseMemory(xml_str.c_str(), xml_str.length());
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

} // namespace duckdb