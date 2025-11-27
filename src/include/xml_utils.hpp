#pragma once

#include "duckdb.hpp"
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xmlschemas.h>
#include <string>
#include <vector>

namespace duckdb {

// Schema options for XML to JSON conversion
struct XMLToJSONOptions {
	std::vector<std::string> force_list;   // Element names to always convert to arrays
	std::string attr_prefix = "@";         // Prefix for attributes (default "@")
	std::string text_key = "#text";        // Key for text content (default "#text")
	std::string namespaces = "strip";      // Namespace handling: "strip", "expand", "keep" (default "strip")
	std::string xmlns_key = "";            // Key for namespace declarations (default "", empty means disabled)
	std::string empty_elements = "object"; // How to handle empty elements: "object", "null", "string"
};

// Function bind data for XML to JSON with schema
struct XMLToJSONBindData : public FunctionData {
	XMLToJSONOptions options;

	explicit XMLToJSONBindData(XMLToJSONOptions opts) : options(std::move(opts)) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<XMLToJSONBindData>(options);
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<XMLToJSONBindData>();
		return options.force_list == other.options.force_list && options.attr_prefix == other.options.attr_prefix &&
		       options.text_key == other.options.text_key && options.namespaces == other.options.namespaces &&
		       options.empty_elements == other.options.empty_elements;
	}
};

// RAII wrapper for libxml2 resources
struct XMLDocRAII {
	xmlDocPtr doc = nullptr;
	xmlXPathContextPtr xpath_ctx = nullptr;

	XMLDocRAII(const std::string &xml_str);
	XMLDocRAII(const std::string &content, bool is_html);
	~XMLDocRAII();

	// Delete copy operations for safety
	XMLDocRAII(const XMLDocRAII &) = delete;
	XMLDocRAII &operator=(const XMLDocRAII &) = delete;

	// Move operations
	XMLDocRAII(XMLDocRAII &&other) noexcept;
	XMLDocRAII &operator=(XMLDocRAII &&other) noexcept;

	bool IsValid() const {
		return doc != nullptr;
	}
};

// Custom deleters for libxml2 resources to use with DuckDB's smart pointers
struct XMLSchemaParserDeleter {
	void operator()(xmlSchemaParserCtxtPtr ptr) const {
		if (ptr)
			xmlSchemaFreeParserCtxt(ptr);
	}
};

struct XMLSchemaDeleter {
	void operator()(xmlSchemaPtr ptr) const {
		if (ptr)
			xmlSchemaFree(ptr);
	}
};

struct XMLSchemaValidDeleter {
	void operator()(xmlSchemaValidCtxtPtr ptr) const {
		if (ptr)
			xmlSchemaFreeValidCtxt(ptr);
	}
};

struct XMLCharDeleter {
	void operator()(xmlChar *ptr) const {
		if (ptr)
			xmlFree(ptr);
	}
};

struct XMLDocDeleter {
	void operator()(xmlDocPtr ptr) const {
		if (ptr)
			xmlFreeDoc(ptr);
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

// HTML-specific structures
struct HTMLLink {
	std::string text;
	std::string url;
	std::string title;
	int64_t line_number;
};

struct HTMLImage {
	std::string alt_text;
	std::string src;
	std::string title;
	int64_t width;
	int64_t height;
	int64_t line_number;
};

struct HTMLTable {
	std::vector<std::string> headers;
	std::vector<std::vector<std::string>> rows;
	int64_t line_number;
	int64_t num_columns;
	int64_t num_rows;
};

// Utility functions
class XMLUtils {
public:
	// Validation functions
	static bool IsValidXML(const std::string &xml_str);
	static bool IsWellFormedXML(const std::string &xml_str);
	static bool ValidateXMLSchema(const std::string &xml_str, const std::string &xsd_schema);

	// Extraction functions
	static std::vector<XMLElement> ExtractByXPath(const std::string &xml_str, const std::string &xpath);
	static std::string ExtractTextByXPath(const std::string &xml_str, const std::string &xpath);
	static std::vector<XMLComment> ExtractComments(const std::string &xml_str);
	static std::vector<XMLComment> ExtractCData(const std::string &xml_str);
	static std::vector<XMLNamespace> ExtractNamespaces(const std::string &xml_str);

	// Content manipulation
	static std::string PrettyPrintXML(const std::string &xml_str);
	static std::string MinifyXML(const std::string &xml_str);

	// Analysis functions
	static XMLStats GetXMLStats(const std::string &xml_str);

	// Conversion functions
	static std::string XMLToJSON(const std::string &xml_str);
	static std::string XMLToJSON(const std::string &xml_str, const XMLToJSONOptions &options);
	static std::string JSONToXML(const std::string &json_str);
	static std::string ScalarToXML(const std::string &value, const std::string &node_name);

	// XMLFragment extraction
	static std::string ExtractXMLFragment(const std::string &xml_str, const std::string &xpath);
	static std::string ExtractXMLFragmentAll(const std::string &xml_str, const std::string &xpath);

	// Complex type conversion functions for to_xml()
	static void ConvertListToXML(Vector &input_vector, Vector &result, idx_t count, const std::string &node_name);
	static void ConvertStructToXML(Vector &input_vector, Vector &result, idx_t count, const std::string &node_name);

	// Recursive value conversion for nested types - returns xmlNodePtr for direct attachment
	static xmlNodePtr ConvertValueToXMLNode(const Value &value, const LogicalType &type, const std::string &node_name,
	                                        xmlDocPtr doc);

	// HTML-specific extraction functions
	static std::vector<HTMLLink> ExtractHTMLLinks(const std::string &html_str);
	static std::vector<HTMLImage> ExtractHTMLImages(const std::string &html_str);
	static std::vector<HTMLTable> ExtractHTMLTables(const std::string &html_str);
	static std::string ExtractHTMLText(const std::string &html_str, const std::string &selector = "");
	static std::string ExtractHTMLTextByXPath(const std::string &html_str, const std::string &xpath);

	// HTML entity escaping/unescaping
	static std::string HTMLUnescape(const std::string &html_str);
	static std::string HTMLEscape(const std::string &text);

	// Internal helper functions
	static XMLElement ProcessXMLNode(xmlNodePtr node);
	static std::string GetNodePath(xmlNodePtr node);
	static void InitializeLibXML();
	static void CleanupLibXML();
};

} // namespace duckdb
