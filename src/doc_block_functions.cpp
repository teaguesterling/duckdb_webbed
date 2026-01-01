#include "doc_block_functions.hpp"
#include "doc_block_types.hpp"
#include "xml_types.hpp"
#include "xml_utils.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <sstream>
#include <regex>

namespace duckdb {

// Forward declarations for helper functions
static std::string GetNodeTextContent(xmlNodePtr node);
static std::string GetNodeInnerHTML(xmlNodePtr node, xmlDocPtr doc);
static std::string GetNodeAttribute(xmlNodePtr node, const char *attr_name);
static int CountBlockquoteAncestors(xmlNodePtr node);
static std::string ListItemsToJson(xmlNodePtr node);
static std::string TableToJson(xmlNodePtr node);
static bool ContentContainsTags(const std::string &content);
static std::string EscapeJsonString(const std::string &str);

// XPath query for block-level elements
static const char *BLOCK_XPATH = "//body//*[self::h1 or self::h2 or self::h3 or self::h4 or self::h5 or self::h6 "
                                 "or self::p or self::pre or self::blockquote or self::ul or self::ol "
                                 "or self::table or self::hr or self::img or self::figure]";

void DocBlockFunctions::HtmlToDocBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	auto doc_block_list_type = DocBlockTypes::DocBlockListType();

	for (idx_t i = 0; i < count; i++) {
		auto html_value = html_vector.GetValue(i);

		if (html_value.IsNull()) {
			result.SetValue(i, Value::LIST(DocBlockTypes::DocBlockType(), vector<Value>()));
			continue;
		}

		std::string html_str = html_value.GetValue<string>();

		// Parse HTML using libxml2's HTML parser
		htmlDocPtr doc = htmlReadMemory(html_str.c_str(), html_str.length(), nullptr, "UTF-8",
		                                HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

		if (!doc) {
			result.SetValue(i, Value::LIST(DocBlockTypes::DocBlockType(), vector<Value>()));
			continue;
		}

		// Create XPath context
		xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
		if (!xpath_ctx) {
			xmlFreeDoc(doc);
			result.SetValue(i, Value::LIST(DocBlockTypes::DocBlockType(), vector<Value>()));
			continue;
		}

		// Execute XPath query
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST BLOCK_XPATH, xpath_ctx);

		vector<Value> blocks;
		int32_t block_order = 0;

		if (xpath_obj && xpath_obj->nodesetval) {
			for (int j = 0; j < xpath_obj->nodesetval->nodeNr; j++) {
				xmlNodePtr node = xpath_obj->nodesetval->nodeTab[j];
				if (!node || !node->name) {
					continue;
				}

				std::string tag(reinterpret_cast<const char *>(node->name));
				std::string content;
				Value level_value;
				std::string block_type;
				std::string encoding = DocBlockTypes::ENCODING_TEXT;
				std::map<std::string, std::string> attrs;

				// Heading: h1-h6
				if (tag.length() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
					block_type = DocBlockTypes::TYPE_HEADING;
					level_value = Value::INTEGER(tag[1] - '0');
					content = GetNodeTextContent(node);
					std::string id = GetNodeAttribute(node, "id");
					if (!id.empty()) {
						attrs["id"] = id;
					}
				}
				// Paragraph
				else if (tag == "p") {
					block_type = DocBlockTypes::TYPE_PARAGRAPH;
					// Check for inline HTML content
					std::string inner_html = GetNodeInnerHTML(node, doc);
					if (ContentContainsTags(inner_html)) {
						content = inner_html;
						encoding = DocBlockTypes::ENCODING_HTML;
					} else {
						content = GetNodeTextContent(node);
					}
				}
				// Code block (pre with optional code child)
				else if (tag == "pre") {
					block_type = DocBlockTypes::TYPE_CODE;
					content = GetNodeTextContent(node);

					// Look for language class on <code> child
					xmlNodePtr code_child = node->children;
					while (code_child) {
						if (code_child->type == XML_ELEMENT_NODE && xmlStrcmp(code_child->name, BAD_CAST "code") == 0) {
							std::string cls = GetNodeAttribute(code_child, "class");
							// Extract language from "language-xxx" or "lang-xxx"
							std::regex lang_regex("(?:language-|lang-)([a-zA-Z0-9_+-]+)");
							std::smatch match;
							if (std::regex_search(cls, match, lang_regex)) {
								attrs["language"] = match[1].str();
							}
							break;
						}
						code_child = code_child->next;
					}
				}
				// Blockquote
				else if (tag == "blockquote") {
					block_type = DocBlockTypes::TYPE_BLOCKQUOTE;
					int depth = CountBlockquoteAncestors(node) + 1;
					level_value = Value::INTEGER(depth);
					content = GetNodeTextContent(node);
				}
				// Unordered list
				else if (tag == "ul") {
					block_type = DocBlockTypes::TYPE_LIST;
					content = ListItemsToJson(node);
					encoding = DocBlockTypes::ENCODING_JSON;
					attrs["ordered"] = "false";
				}
				// Ordered list
				else if (tag == "ol") {
					block_type = DocBlockTypes::TYPE_LIST;
					content = ListItemsToJson(node);
					encoding = DocBlockTypes::ENCODING_JSON;
					attrs["ordered"] = "true";
				}
				// Table
				else if (tag == "table") {
					block_type = DocBlockTypes::TYPE_TABLE;
					content = TableToJson(node);
					encoding = DocBlockTypes::ENCODING_JSON;
				}
				// Horizontal rule
				else if (tag == "hr") {
					block_type = DocBlockTypes::TYPE_HR;
					content = "";
				}
				// Image
				else if (tag == "img") {
					block_type = DocBlockTypes::TYPE_IMAGE;
					std::string src = GetNodeAttribute(node, "src");
					std::string alt = GetNodeAttribute(node, "alt");
					std::string title = GetNodeAttribute(node, "title");
					attrs["src"] = src;
					if (!alt.empty()) {
						attrs["alt"] = alt;
						content = alt;
					}
					if (!title.empty()) {
						attrs["title"] = title;
					}
				}
				// Figure (contains img)
				else if (tag == "figure") {
					block_type = DocBlockTypes::TYPE_IMAGE;
					// Look for img child
					xmlNodePtr img_child = node->children;
					while (img_child) {
						if (img_child->type == XML_ELEMENT_NODE && xmlStrcmp(img_child->name, BAD_CAST "img") == 0) {
							std::string src = GetNodeAttribute(img_child, "src");
							std::string alt = GetNodeAttribute(img_child, "alt");
							attrs["src"] = src;
							if (!alt.empty()) {
								attrs["alt"] = alt;
								content = alt;
							}
							break;
						}
						img_child = img_child->next;
					}
					// Look for figcaption
					xmlNodePtr caption_child = node->children;
					while (caption_child) {
						if (caption_child->type == XML_ELEMENT_NODE &&
						    xmlStrcmp(caption_child->name, BAD_CAST "figcaption") == 0) {
							std::string caption = GetNodeTextContent(caption_child);
							if (!caption.empty()) {
								attrs["title"] = caption;
							}
							break;
						}
						caption_child = caption_child->next;
					}
				} else {
					// Skip unknown elements
					continue;
				}

				blocks.push_back(
				    DocBlockTypes::CreateBlock(block_type, content, level_value, encoding, attrs, block_order++));
			}
		}

		if (xpath_obj) {
			xmlXPathFreeObject(xpath_obj);
		}
		xmlXPathFreeContext(xpath_ctx);
		xmlFreeDoc(doc);

		result.SetValue(i, Value::LIST(DocBlockTypes::DocBlockType(), blocks));
	}
}

void DocBlockFunctions::DocBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vector = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_value = blocks_vector.GetValue(i);

		if (blocks_value.IsNull()) {
			result.SetValue(i, Value(""));
			continue;
		}

		std::stringstream html;
		auto &blocks_list = ListValue::GetChildren(blocks_value);

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto &struct_values = StructValue::GetChildren(block);
			std::string block_type = struct_values[DocBlockTypes::BLOCK_TYPE_IDX].GetValue<string>();
			std::string content = struct_values[DocBlockTypes::CONTENT_IDX].GetValue<string>();
			Value level_val = struct_values[DocBlockTypes::LEVEL_IDX];
			std::string encoding = struct_values[DocBlockTypes::ENCODING_IDX].GetValue<string>();
			Value attrs_val = struct_values[DocBlockTypes::ATTRIBUTES_IDX];

			// Extract attributes from MAP
			std::map<std::string, std::string> attrs;
			if (!attrs_val.IsNull()) {
				auto &map_children = MapValue::GetChildren(attrs_val);
				for (auto &entry : map_children) {
					auto &entry_struct = StructValue::GetChildren(entry);
					std::string key = entry_struct[0].GetValue<string>();
					std::string value = entry_struct[1].GetValue<string>();
					attrs[key] = value;
				}
			}

			// Generate HTML based on block type
			if (block_type == DocBlockTypes::TYPE_HEADING) {
				int lvl = level_val.IsNull() ? 1 : level_val.GetValue<int32_t>();
				if (lvl < 1)
					lvl = 1;
				if (lvl > 6)
					lvl = 6;
				std::string id_attr = "";
				if (attrs.count("id")) {
					id_attr = " id=\"" + XMLUtils::HTMLEscape(attrs["id"]) + "\"";
				}
				html << "<h" << lvl << id_attr << ">";
				if (encoding == DocBlockTypes::ENCODING_HTML) {
					html << content;
				} else {
					html << XMLUtils::HTMLEscape(content);
				}
				html << "</h" << lvl << ">";
			} else if (block_type == DocBlockTypes::TYPE_PARAGRAPH) {
				html << "<p>";
				if (encoding == DocBlockTypes::ENCODING_HTML) {
					html << content;
				} else {
					html << XMLUtils::HTMLEscape(content);
				}
				html << "</p>";
			} else if (block_type == DocBlockTypes::TYPE_CODE) {
				std::string lang_class = "";
				if (attrs.count("language")) {
					lang_class = " class=\"language-" + XMLUtils::HTMLEscape(attrs["language"]) + "\"";
				}
				html << "<pre><code" << lang_class << ">" << XMLUtils::HTMLEscape(content) << "</code></pre>";
			} else if (block_type == DocBlockTypes::TYPE_BLOCKQUOTE) {
				int depth = level_val.IsNull() ? 1 : level_val.GetValue<int32_t>();
				if (depth < 1)
					depth = 1;
				for (int d = 0; d < depth; d++) {
					html << "<blockquote>";
				}
				if (encoding == DocBlockTypes::ENCODING_HTML) {
					html << content;
				} else {
					html << XMLUtils::HTMLEscape(content);
				}
				for (int d = 0; d < depth; d++) {
					html << "</blockquote>";
				}
			} else if (block_type == DocBlockTypes::TYPE_LIST) {
				bool ordered = attrs.count("ordered") && attrs["ordered"] == "true";
				std::string tag = ordered ? "ol" : "ul";
				html << "<" << tag << ">";
				// Parse JSON content as array
				if (encoding == DocBlockTypes::ENCODING_JSON && !content.empty()) {
					// Simple JSON array parsing for list items
					std::regex item_regex("\"([^\"\\\\]*(\\\\.[^\"\\\\]*)*)\"");
					std::sregex_iterator iter(content.begin(), content.end(), item_regex);
					std::sregex_iterator end;
					while (iter != end) {
						std::string item = (*iter)[1].str();
						// Unescape JSON string
						std::string unescaped;
						for (size_t k = 0; k < item.size(); k++) {
							if (item[k] == '\\' && k + 1 < item.size()) {
								char next = item[k + 1];
								if (next == 'n')
									unescaped += '\n';
								else if (next == 'r')
									unescaped += '\r';
								else if (next == 't')
									unescaped += '\t';
								else if (next == '"')
									unescaped += '"';
								else if (next == '\\')
									unescaped += '\\';
								else
									unescaped += next;
								k++;
							} else {
								unescaped += item[k];
							}
						}
						html << "<li>" << XMLUtils::HTMLEscape(unescaped) << "</li>";
						++iter;
					}
				}
				html << "</" << tag << ">";
			} else if (block_type == DocBlockTypes::TYPE_TABLE) {
				// For now, output a placeholder or parse JSON table
				html << "<table>";
				// TODO: Parse JSON table content and render rows/cells
				html << "</table>";
			} else if (block_type == DocBlockTypes::TYPE_HR) {
				html << "<hr>";
			} else if (block_type == DocBlockTypes::TYPE_IMAGE) {
				std::string src = attrs.count("src") ? attrs["src"] : "";
				std::string alt = attrs.count("alt") ? attrs["alt"] : "";
				std::string title = attrs.count("title") ? attrs["title"] : "";
				html << "<img src=\"" << XMLUtils::HTMLEscape(src) << "\"";
				if (!alt.empty()) {
					html << " alt=\"" << XMLUtils::HTMLEscape(alt) << "\"";
				}
				if (!title.empty()) {
					html << " title=\"" << XMLUtils::HTMLEscape(title) << "\"";
				}
				html << ">";
			} else if (block_type == DocBlockTypes::TYPE_RAW) {
				// Pass through raw content
				html << content;
			} else if (block_type == DocBlockTypes::TYPE_METADATA) {
				// Output as HTML comment
				html << "<!-- " << XMLUtils::HTMLEscape(content) << " -->";
			}
		}

		result.SetValue(i, Value(html.str()));
	}
}

void DocBlockFunctions::Register(ExtensionLoader &loader) {
	// html_to_doc_blocks(html HTML) -> LIST(doc_block)
	ScalarFunctionSet html_to_doc_blocks_set("html_to_doc_blocks");
	html_to_doc_blocks_set.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType()}, DocBlockTypes::DocBlockListType(), HtmlToDocBlocksFunction));
	html_to_doc_blocks_set.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR}, DocBlockTypes::DocBlockListType(), HtmlToDocBlocksFunction));
	loader.RegisterFunction(html_to_doc_blocks_set);

	// doc_blocks_to_html(blocks LIST(doc_block)) -> HTML
	auto doc_blocks_to_html_func = ScalarFunction("doc_blocks_to_html", {DocBlockTypes::DocBlockListType()},
	                                              XMLTypes::HTMLType(), DocBlocksToHtmlFunction);
	loader.RegisterFunction(doc_blocks_to_html_func);
}

// ============================================================================
// Helper functions
// ============================================================================

static std::string GetNodeTextContent(xmlNodePtr node) {
	if (!node) {
		return "";
	}
	xmlChar *content = xmlNodeGetContent(node);
	if (!content) {
		return "";
	}
	std::string result(reinterpret_cast<const char *>(content));
	xmlFree(content);
	return result;
}

static std::string GetNodeInnerHTML(xmlNodePtr node, xmlDocPtr doc) {
	if (!node) {
		return "";
	}
	std::string result;
	xmlNodePtr child = node->children;
	while (child) {
		xmlBufferPtr buffer = xmlBufferCreate();
		if (buffer) {
			xmlNodeDump(buffer, doc, child, 0, 0);
			result += std::string(reinterpret_cast<const char *>(xmlBufferContent(buffer)));
			xmlBufferFree(buffer);
		}
		child = child->next;
	}
	return result;
}

static std::string GetNodeAttribute(xmlNodePtr node, const char *attr_name) {
	if (!node) {
		return "";
	}
	xmlChar *value = xmlGetProp(node, BAD_CAST attr_name);
	if (!value) {
		return "";
	}
	std::string result(reinterpret_cast<const char *>(value));
	xmlFree(value);
	return result;
}

static int CountBlockquoteAncestors(xmlNodePtr node) {
	int count = 0;
	xmlNodePtr parent = node->parent;
	while (parent) {
		if (parent->type == XML_ELEMENT_NODE && parent->name && xmlStrcmp(parent->name, BAD_CAST "blockquote") == 0) {
			count++;
		}
		parent = parent->parent;
	}
	return count;
}

static std::string EscapeJsonString(const std::string &str) {
	std::string result;
	for (char c : str) {
		switch (c) {
		case '"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			result += c;
			break;
		}
	}
	return result;
}

static std::string ListItemsToJson(xmlNodePtr node) {
	std::string json = "[";
	bool first = true;

	xmlNodePtr child = node->children;
	while (child) {
		if (child->type == XML_ELEMENT_NODE && child->name && xmlStrcmp(child->name, BAD_CAST "li") == 0) {
			if (!first) {
				json += ",";
			}
			first = false;
			std::string item_text = GetNodeTextContent(child);
			json += "\"" + EscapeJsonString(item_text) + "\"";
		}
		child = child->next;
	}

	json += "]";
	return json;
}

static std::string TableToJson(xmlNodePtr node) {
	// Extract table as JSON with headers and rows
	std::string json = "{\"headers\":[],\"rows\":[]}";

	std::vector<std::string> headers;
	std::vector<std::vector<std::string>> rows;

	// Look for thead/tbody or direct tr children
	xmlNodePtr child = node->children;
	while (child) {
		if (child->type == XML_ELEMENT_NODE && child->name) {
			if (xmlStrcmp(child->name, BAD_CAST "thead") == 0) {
				// Extract headers from thead
				xmlNodePtr tr = child->children;
				while (tr) {
					if (tr->type == XML_ELEMENT_NODE && tr->name && xmlStrcmp(tr->name, BAD_CAST "tr") == 0) {
						xmlNodePtr th = tr->children;
						while (th) {
							if (th->type == XML_ELEMENT_NODE && th->name &&
							    (xmlStrcmp(th->name, BAD_CAST "th") == 0 || xmlStrcmp(th->name, BAD_CAST "td") == 0)) {
								headers.push_back(GetNodeTextContent(th));
							}
							th = th->next;
						}
						break; // Only first row of headers
					}
					tr = tr->next;
				}
			} else if (xmlStrcmp(child->name, BAD_CAST "tbody") == 0) {
				// Extract rows from tbody
				xmlNodePtr tr = child->children;
				while (tr) {
					if (tr->type == XML_ELEMENT_NODE && tr->name && xmlStrcmp(tr->name, BAD_CAST "tr") == 0) {
						std::vector<std::string> row;
						xmlNodePtr td = tr->children;
						while (td) {
							if (td->type == XML_ELEMENT_NODE && td->name &&
							    (xmlStrcmp(td->name, BAD_CAST "td") == 0 || xmlStrcmp(td->name, BAD_CAST "th") == 0)) {
								row.push_back(GetNodeTextContent(td));
							}
							td = td->next;
						}
						if (!row.empty()) {
							rows.push_back(row);
						}
					}
					tr = tr->next;
				}
			} else if (xmlStrcmp(child->name, BAD_CAST "tr") == 0) {
				// Direct tr child (no thead/tbody)
				std::vector<std::string> row;
				xmlNodePtr td = child->children;
				while (td) {
					if (td->type == XML_ELEMENT_NODE && td->name &&
					    (xmlStrcmp(td->name, BAD_CAST "td") == 0 || xmlStrcmp(td->name, BAD_CAST "th") == 0)) {
						row.push_back(GetNodeTextContent(td));
					}
					td = td->next;
				}
				if (!row.empty()) {
					if (headers.empty()) {
						headers = row;
					} else {
						rows.push_back(row);
					}
				}
			}
		}
		child = child->next;
	}

	// Build JSON
	std::stringstream ss;
	ss << "{\"headers\":[";
	for (size_t i = 0; i < headers.size(); i++) {
		if (i > 0)
			ss << ",";
		ss << "\"" << EscapeJsonString(headers[i]) << "\"";
	}
	ss << "],\"rows\":[";
	for (size_t i = 0; i < rows.size(); i++) {
		if (i > 0)
			ss << ",";
		ss << "[";
		for (size_t j = 0; j < rows[i].size(); j++) {
			if (j > 0)
				ss << ",";
			ss << "\"" << EscapeJsonString(rows[i][j]) << "\"";
		}
		ss << "]";
	}
	ss << "]}";

	return ss.str();
}

static bool ContentContainsTags(const std::string &content) {
	// Simple check for HTML tags
	return content.find('<') != std::string::npos && content.find('>') != std::string::npos;
}

} // namespace duckdb
