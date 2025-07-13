#pragma once

#include "duckdb.hpp"
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xmlschemas.h>
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

// Custom deleters for libxml2 resources to use with DuckDB's smart pointers
struct XMLSchemaParserDeleter {
	void operator()(xmlSchemaParserCtxtPtr ptr) const {
		if (ptr) xmlSchemaFreeParserCtxt(ptr);
	}
};

struct XMLSchemaDeleter {
	void operator()(xmlSchemaPtr ptr) const {
		if (ptr) xmlSchemaFree(ptr);
	}
};

struct XMLSchemaValidDeleter {
	void operator()(xmlSchemaValidCtxtPtr ptr) const {
		if (ptr) xmlSchemaFreeValidCtxt(ptr);
	}
};

struct XMLCharDeleter {
	void operator()(xmlChar* ptr) const {
		if (ptr) xmlFree(ptr);
	}
};

struct XMLDocDeleter {
	void operator()(xmlDocPtr ptr) const {
		if (ptr) xmlFreeDoc(ptr);
	}
};

// Type aliases for DuckDB-style smart pointers
using XMLSchemaParserPtr = std::unique_ptr<xmlSchemaParserCtxt, XMLSchemaParserDeleter>;
using XMLSchemaPtr = std::unique_ptr<xmlSchema, XMLSchemaDeleter>;
using XMLSchemaValidPtr = std::unique_ptr<xmlSchemaValidCtxt, XMLSchemaValidDeleter>;
using XMLCharPtr = std::unique_ptr<xmlChar, XMLCharDeleter>;
using XMLDocPtr = std::unique_ptr<xmlDoc, XMLDocDeleter>;

// Structure to hold extracted XML element information
struct XMLElement {
	std::string name;
	std::string text_content;
	std::map<std::string, std::string> attributes;
	std::string namespace_uri;
	std::string path;
	int64_t line_number;
};

// Structure for XML comments and CDATA
struct XMLComment {
	std::string content;
	int64_t line_number;
};

// Structure for XML namespaces
struct XMLNamespace {
	std::string prefix;
	std::string uri;
};

// Structure for XML document statistics
struct XMLStats {
	int64_t element_count;
	int64_t attribute_count;
	int64_t max_depth;
	int64_t size_bytes;
	int64_t namespace_count;
};

// Utility functions
class XMLUtils {
public:
	// Validation functions
	static bool IsValidXML(const std::string& xml_str);
	static bool IsWellFormedXML(const std::string& xml_str);
	static bool ValidateXMLSchema(const std::string& xml_str, const std::string& xsd_schema);
	
	// Extraction functions
	static std::vector<XMLElement> ExtractByXPath(const std::string& xml_str, const std::string& xpath);
	static std::string ExtractTextByXPath(const std::string& xml_str, const std::string& xpath);
	static std::vector<XMLComment> ExtractComments(const std::string& xml_str);
	static std::vector<XMLComment> ExtractCData(const std::string& xml_str);
	static std::vector<XMLNamespace> ExtractNamespaces(const std::string& xml_str);
	
	// Content manipulation
	static std::string PrettyPrintXML(const std::string& xml_str);
	static std::string MinifyXML(const std::string& xml_str);
	
	// Analysis functions
	static XMLStats GetXMLStats(const std::string& xml_str);
	
	// Conversion functions
	static std::string XMLToJSON(const std::string& xml_str);
	static std::string JSONToXML(const std::string& json_str);
	static std::string ScalarToXML(const std::string& value, const std::string& node_name);
	
	// Complex type conversion functions for to_xml()
	static void ConvertListToXML(Vector &input_vector, Vector &result, idx_t count, const std::string& node_name);
	static void ConvertStructToXML(Vector &input_vector, Vector &result, idx_t count, const std::string& node_name);
	
	// Internal helper functions
	static XMLElement ProcessXMLNode(xmlNodePtr node);
	static std::string GetNodePath(xmlNodePtr node);
	static void InitializeLibXML();
	static void CleanupLibXML();
};

} // namespace duckdb