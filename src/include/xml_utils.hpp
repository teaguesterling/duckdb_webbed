#pragma once

#include "duckdb.hpp"
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <string>
#include <vector>

namespace duckdb {

// RAII wrapper for libxml2 resources
struct XMLDocRAII {
	xmlDocPtr doc = nullptr;
	xmlXPathContextPtr xpath_ctx = nullptr;
	
	XMLDocRAII(const std::string& xml_str);
	~XMLDocRAII();
	
	// Delete copy operations for safety
	XMLDocRAII(const XMLDocRAII&) = delete;
	XMLDocRAII& operator=(const XMLDocRAII&) = delete;
	
	// Move operations
	XMLDocRAII(XMLDocRAII&& other) noexcept;
	XMLDocRAII& operator=(XMLDocRAII&& other) noexcept;
	
	bool IsValid() const { return doc != nullptr; }
};

// Structure to hold extracted XML element information
struct XMLElement {
	std::string name;
	std::string text_content;
	std::map<std::string, std::string> attributes;
	std::string namespace_uri;
	std::string path;
	int64_t line_number;
};

// Utility functions
class XMLUtils {
public:
	static bool IsValidXML(const std::string& xml_str);
	static bool IsWellFormedXML(const std::string& xml_str);
	static std::vector<XMLElement> ExtractByXPath(const std::string& xml_str, const std::string& xpath);
	static std::string ExtractTextByXPath(const std::string& xml_str, const std::string& xpath);
	static XMLElement ProcessXMLNode(xmlNodePtr node);
	static std::string GetNodePath(xmlNodePtr node);
	static void InitializeLibXML();
	static void CleanupLibXML();
};

} // namespace duckdb