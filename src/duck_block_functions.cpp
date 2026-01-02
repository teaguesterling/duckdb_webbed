#include "duck_block_functions.hpp"
#include "duck_block_types.hpp"
#include "xml_types.hpp"
#include "xml_utils.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <sstream>
#include <regex>
#include <set>

namespace duckdb {

// Forward declarations for helper functions
static std::string GetNodeTextContent(xmlNodePtr node);
static std::string GetNodeInnerHTML(xmlNodePtr node, xmlDocPtr doc);
static std::string GetNodeAttribute(xmlNodePtr node, const char *attr_name);
static int CountBlockquoteAncestors(xmlNodePtr node);
static std::string ListItemsToJson(xmlNodePtr node);
static std::string TableToJson(xmlNodePtr node);
static std::string TableJsonToHtml(const std::string &json);
static bool ContentContainsTags(const std::string &content);
static std::string EscapeJsonString(const std::string &str);
static std::string UnescapeJsonString(const std::string &str);

// XPath query for block-level elements
static const char *BLOCK_XPATH = "//body//*[self::h1 or self::h2 or self::h3 or self::h4 or self::h5 or self::h6 "
                                 "or self::p or self::pre or self::blockquote or self::ul or self::ol "
                                 "or self::table or self::hr or self::img or self::figure]";

// XPath query for frontmatter script blocks
static const char *FRONTMATTER_XPATH = "//script[@type='application/vnd.frontmatter+yaml']";

// Helper to extract attributes MAP into std::map
static std::map<std::string, std::string> ExtractAttributes(const Value &attrs_val) {
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
	return attrs;
}

// Render inline element to HTML
static std::string RenderInlineElementToHtml(const std::string &element_type, const std::string &content,
                                              const std::map<std::string, std::string> &attrs) {
	if (element_type == DuckBlockTypes::INLINE_TEXT) {
		return XMLUtils::HTMLEscape(content);
	} else if (element_type == DuckBlockTypes::INLINE_BOLD || element_type == "strong") {
		return "<strong>" + XMLUtils::HTMLEscape(content) + "</strong>";
	} else if (element_type == DuckBlockTypes::INLINE_ITALIC || element_type == "em" || element_type == "emphasis") {
		return "<em>" + XMLUtils::HTMLEscape(content) + "</em>";
	} else if (element_type == DuckBlockTypes::INLINE_CODE) {
		return "<code>" + XMLUtils::HTMLEscape(content) + "</code>";
	} else if (element_type == DuckBlockTypes::INLINE_LINK) {
		std::string href = attrs.count("href") ? attrs.at("href") : "";
		std::string title = attrs.count("title") ? attrs.at("title") : "";
		std::string result = "<a href=\"" + XMLUtils::HTMLEscape(href) + "\"";
		if (!title.empty()) {
			result += " title=\"" + XMLUtils::HTMLEscape(title) + "\"";
		}
		result += ">" + XMLUtils::HTMLEscape(content) + "</a>";
		return result;
	} else if (element_type == DuckBlockTypes::INLINE_IMAGE) {
		std::string src = attrs.count("src") ? attrs.at("src") : "";
		std::string alt = content.empty() && attrs.count("alt") ? attrs.at("alt") : content;
		std::string title = attrs.count("title") ? attrs.at("title") : "";
		std::string result = "<img src=\"" + XMLUtils::HTMLEscape(src) + "\"";
		if (!alt.empty()) {
			result += " alt=\"" + XMLUtils::HTMLEscape(alt) + "\"";
		}
		if (!title.empty()) {
			result += " title=\"" + XMLUtils::HTMLEscape(title) + "\"";
		}
		result += ">";
		return result;
	} else if (element_type == DuckBlockTypes::INLINE_SPACE) {
		return " ";
	} else if (element_type == DuckBlockTypes::INLINE_SOFTBREAK) {
		return "\n";
	} else if (element_type == DuckBlockTypes::INLINE_LINEBREAK || element_type == "br") {
		return "<br>";
	} else if (element_type == DuckBlockTypes::INLINE_STRIKETHROUGH || element_type == "del") {
		return "<del>" + XMLUtils::HTMLEscape(content) + "</del>";
	} else if (element_type == DuckBlockTypes::INLINE_SUPERSCRIPT || element_type == "sup") {
		return "<sup>" + XMLUtils::HTMLEscape(content) + "</sup>";
	} else if (element_type == DuckBlockTypes::INLINE_SUBSCRIPT || element_type == "sub") {
		return "<sub>" + XMLUtils::HTMLEscape(content) + "</sub>";
	} else if (element_type == DuckBlockTypes::INLINE_UNDERLINE || element_type == "u") {
		return "<u>" + XMLUtils::HTMLEscape(content) + "</u>";
	} else if (element_type == DuckBlockTypes::INLINE_SMALLCAPS) {
		return "<span style=\"font-variant: small-caps\">" + XMLUtils::HTMLEscape(content) + "</span>";
	} else if (element_type == DuckBlockTypes::INLINE_SPAN) {
		std::string id = attrs.count("id") ? attrs.at("id") : "";
		std::string cls = attrs.count("class") ? attrs.at("class") : "";
		std::string result = "<span";
		if (!id.empty()) {
			result += " id=\"" + XMLUtils::HTMLEscape(id) + "\"";
		}
		if (!cls.empty()) {
			result += " class=\"" + XMLUtils::HTMLEscape(cls) + "\"";
		}
		result += ">" + XMLUtils::HTMLEscape(content) + "</span>";
		return result;
	} else if (element_type == DuckBlockTypes::INLINE_RAW) {
		return content; // Pass through raw HTML
	}
	// Default: return escaped content
	return XMLUtils::HTMLEscape(content);
}

// Extract inline elements from an HTML node's children
// Returns a vector of inline element Values and updates element_order
static std::vector<Value> ExtractInlineElements(xmlNodePtr parent_node, int32_t base_level, int32_t &element_order) {
	std::vector<Value> inlines;

	for (xmlNodePtr child = parent_node->children; child; child = child->next) {
		if (child->type == XML_TEXT_NODE) {
			std::string text = reinterpret_cast<const char *>(child->content);
			// Skip empty text nodes or whitespace-only between elements
			if (!text.empty()) {
				std::map<std::string, std::string> attrs;
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_TEXT, text,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			}
		} else if (child->type == XML_ELEMENT_NODE) {
			std::string tag = reinterpret_cast<const char *>(child->name);
			std::string content = GetNodeTextContent(child);
			std::map<std::string, std::string> attrs;

			if (tag == "strong" || tag == "b") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_BOLD, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "em" || tag == "i") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_ITALIC, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "code") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_CODE, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "a") {
				std::string href = GetNodeAttribute(child, "href");
				if (!href.empty()) {
					attrs["href"] = href;
				}
				std::string title = GetNodeAttribute(child, "title");
				if (!title.empty()) {
					attrs["title"] = title;
				}
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_LINK, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "img") {
				std::string src = GetNodeAttribute(child, "src");
				std::string alt = GetNodeAttribute(child, "alt");
				std::string title = GetNodeAttribute(child, "title");
				if (!src.empty()) {
					attrs["src"] = src;
				}
				if (!alt.empty()) {
					attrs["alt"] = alt;
				}
				if (!title.empty()) {
					attrs["title"] = title;
				}
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_IMAGE, alt,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "br") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_LINEBREAK, "",
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "del" || tag == "s" || tag == "strike") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_STRIKETHROUGH, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "sup") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_SUPERSCRIPT, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "sub") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_SUBSCRIPT, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "u") {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_UNDERLINE, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else if (tag == "span") {
				std::string id = GetNodeAttribute(child, "id");
				std::string cls = GetNodeAttribute(child, "class");
				if (!id.empty()) {
					attrs["id"] = id;
				}
				if (!cls.empty()) {
					attrs["class"] = cls;
				}
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_SPAN, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			} else {
				// Unknown inline element - treat as text
				if (!content.empty()) {
					inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_TEXT, content,
					                                               Value::INTEGER(base_level),
					                                               DuckBlockTypes::ENCODING_TEXT, attrs,
					                                               element_order++));
				}
			}
		}
	}
	return inlines;
}

void DuckBlockFunctions::HtmlToDuckBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	auto duck_block_list_type = DuckBlockTypes::DuckBlockListType();

	for (idx_t i = 0; i < count; i++) {
		auto html_value = html_vector.GetValue(i);

		if (html_value.IsNull()) {
			result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		std::string html_str = html_value.GetValue<string>();

		// Parse HTML using libxml2's HTML parser
		htmlDocPtr doc = htmlReadMemory(html_str.c_str(), html_str.length(), nullptr, "UTF-8",
		                                HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

		if (!doc) {
			result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		// Create XPath context
		xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
		if (!xpath_ctx) {
			xmlFreeDoc(doc);
			result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		vector<Value> blocks;
		int32_t block_order = 0;

		// First, extract frontmatter script blocks
		xmlXPathObjectPtr frontmatter_obj = xmlXPathEvalExpression(BAD_CAST FRONTMATTER_XPATH, xpath_ctx);
		if (frontmatter_obj && frontmatter_obj->nodesetval) {
			for (int j = 0; j < frontmatter_obj->nodesetval->nodeNr; j++) {
				xmlNodePtr node = frontmatter_obj->nodesetval->nodeTab[j];
				if (!node) {
					continue;
				}
				// Extract text content - preserve exactly for lossless round-trip
				std::string content = GetNodeTextContent(node);
				// Trim leading/trailing newlines that we add in doc_blocks_to_html
				if (!content.empty() && content.front() == '\n') {
					content.erase(0, 1);
				}
				if (!content.empty() && content.back() == '\n') {
					content.pop_back();
				}
				std::map<std::string, std::string> attrs;
				blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_METADATA, content,
				                                            DuckBlockTypes::ENCODING_YAML, attrs, block_order++));
			}
		}
		if (frontmatter_obj) {
			xmlXPathFreeObject(frontmatter_obj);
		}

		// Execute XPath query for block-level elements
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST BLOCK_XPATH, xpath_ctx);

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
				std::string encoding = DuckBlockTypes::ENCODING_TEXT;
				std::map<std::string, std::string> attrs;

				// Heading: h1-h6
				if (tag.length() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
					block_type = DuckBlockTypes::TYPE_HEADING;
					// Store heading level in attributes, not in the level field
					attrs[DuckBlockTypes::ATTR_HEADING_LEVEL] = std::string(1, tag[1]);
					std::string id = GetNodeAttribute(node, "id");
					if (!id.empty()) {
						attrs["id"] = id;
					}
					// Check for inline HTML content
					std::string inner_html = GetNodeInnerHTML(node, doc);
					if (ContentContainsTags(inner_html)) {
						// Extract structured inline elements instead of storing raw HTML
						blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_HEADING, "",
						                                             DuckBlockTypes::ENCODING_TEXT, attrs,
						                                             block_order++));
						// Extract inline children at level 1
						auto inline_elements = ExtractInlineElements(node, 1, block_order);
						blocks.insert(blocks.end(), inline_elements.begin(), inline_elements.end());
						continue; // Skip default block creation
					} else {
						content = GetNodeTextContent(node);
					}
				}
				// Paragraph
				else if (tag == "p") {
					block_type = DuckBlockTypes::TYPE_PARAGRAPH;
					// Check for inline HTML content
					std::string inner_html = GetNodeInnerHTML(node, doc);
					if (ContentContainsTags(inner_html)) {
						// Extract structured inline elements instead of storing raw HTML
						// Create paragraph block with empty content
						blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_PARAGRAPH, "",
						                                             DuckBlockTypes::ENCODING_TEXT, attrs,
						                                             block_order++));
						// Extract inline children at level 1
						auto inline_elements = ExtractInlineElements(node, 1, block_order);
						blocks.insert(blocks.end(), inline_elements.begin(), inline_elements.end());
						continue; // Skip default block creation
					} else {
						content = GetNodeTextContent(node);
					}
				}
				// Code block (pre with optional code child)
				else if (tag == "pre") {
					block_type = DuckBlockTypes::TYPE_CODE;
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
					block_type = DuckBlockTypes::TYPE_BLOCKQUOTE;
					int depth = CountBlockquoteAncestors(node) + 1;
					level_value = Value::INTEGER(depth);
					content = GetNodeTextContent(node);
				}
				// Unordered list
				else if (tag == "ul") {
					block_type = DuckBlockTypes::TYPE_LIST;
					content = ListItemsToJson(node);
					encoding = DuckBlockTypes::ENCODING_JSON;
					attrs["ordered"] = "false";
				}
				// Ordered list
				else if (tag == "ol") {
					block_type = DuckBlockTypes::TYPE_LIST;
					content = ListItemsToJson(node);
					encoding = DuckBlockTypes::ENCODING_JSON;
					attrs["ordered"] = "true";
				}
				// Table
				else if (tag == "table") {
					block_type = DuckBlockTypes::TYPE_TABLE;
					content = TableToJson(node);
					encoding = DuckBlockTypes::ENCODING_JSON;
				}
				// Horizontal rule
				else if (tag == "hr") {
					block_type = DuckBlockTypes::TYPE_HR;
					content = "";
				}
				// Image
				else if (tag == "img") {
					block_type = DuckBlockTypes::TYPE_IMAGE;
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
					block_type = DuckBlockTypes::TYPE_IMAGE;
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
				    DuckBlockTypes::CreateBlock(block_type, content, level_value, encoding, attrs, block_order++));
			}
		}

		if (xpath_obj) {
			xmlXPathFreeObject(xpath_obj);
		}
		xmlXPathFreeContext(xpath_ctx);
		xmlFreeDoc(doc);

		result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), blocks));
	}
}

void DuckBlockFunctions::DuckBlocksToHtmlFunction(DataChunk &args, ExpressionState &state, Vector &result) {
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
		std::set<size_t> consumed_indices; // Track consumed inline children

		// Use index-based iteration for consuming look-ahead
		for (size_t block_idx = 0; block_idx < blocks_list.size(); block_idx++) {
			// Skip if already consumed as inline child
			if (consumed_indices.count(block_idx)) {
				continue;
			}

			auto &block = blocks_list[block_idx];
			if (block.IsNull()) {
				continue;
			}

			auto &struct_values = StructValue::GetChildren(block);
			// Struct format: kind, element_type, content, level, encoding, attributes, element_order
			std::string kind = struct_values[DuckBlockTypes::KIND_IDX].GetValue<string>();
			std::string element_type = struct_values[DuckBlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
			std::string content = struct_values[DuckBlockTypes::CONTENT_IDX].GetValue<string>();
			Value level_val = struct_values[DuckBlockTypes::LEVEL_IDX];
			std::string encoding = struct_values[DuckBlockTypes::ENCODING_IDX].GetValue<string>();
			Value attrs_val = struct_values[DuckBlockTypes::ATTRIBUTES_IDX];

			// Extract attributes from MAP using helper
			std::map<std::string, std::string> attrs = ExtractAttributes(attrs_val);

			// Check if this is an inline element
			if (kind == DuckBlockTypes::KIND_INLINE) {
				// Render standalone inline element
				html << RenderInlineElementToHtml(element_type, content, attrs);
				continue;
			}

			// Generate HTML based on block element type
			if (element_type == DuckBlockTypes::TYPE_HEADING) {
				// Read heading level from attributes
				int lvl = 1;
				if (attrs.count(DuckBlockTypes::ATTR_HEADING_LEVEL)) {
					lvl = std::stoi(attrs[DuckBlockTypes::ATTR_HEADING_LEVEL]);
				}
				if (lvl < 1)
					lvl = 1;
				if (lvl > 6)
					lvl = 6;
				std::string id_attr = "";
				if (attrs.count("id")) {
					id_attr = " id=\"" + XMLUtils::HTMLEscape(attrs["id"]) + "\"";
				}
				html << "<h" << lvl << id_attr << ">";

				// Render block's own content if present
				if (!content.empty()) {
					if (encoding == DuckBlockTypes::ENCODING_HTML) {
						html << content;
					} else {
						html << XMLUtils::HTMLEscape(content);
					}
				}

				// Consuming look-ahead: collect and render inline children at level >= 1
				for (size_t next_idx = block_idx + 1; next_idx < blocks_list.size(); next_idx++) {
					auto &next_block = blocks_list[next_idx];
					if (next_block.IsNull()) {
						continue;
					}
					auto &next_struct = StructValue::GetChildren(next_block);
					std::string next_kind = next_struct[DuckBlockTypes::KIND_IDX].GetValue<string>();
					Value next_level = next_struct[DuckBlockTypes::LEVEL_IDX];

					// Stop if we hit a block or an element back at base level (NULL)
					if (next_kind != DuckBlockTypes::KIND_INLINE) {
						break;
					}
					if (next_level.IsNull() || next_level.GetValue<int32_t>() < 1) {
						break;
					}

					// Render this inline child
					std::string next_type = next_struct[DuckBlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
					std::string next_content = next_struct[DuckBlockTypes::CONTENT_IDX].GetValue<string>();
					auto next_attrs = ExtractAttributes(next_struct[DuckBlockTypes::ATTRIBUTES_IDX]);
					html << RenderInlineElementToHtml(next_type, next_content, next_attrs);

					consumed_indices.insert(next_idx);
				}

				html << "</h" << lvl << ">";
			} else if (element_type == DuckBlockTypes::TYPE_PARAGRAPH) {
				html << "<p>";

				// Render block's own content if present
				if (!content.empty()) {
					if (encoding == DuckBlockTypes::ENCODING_HTML) {
						html << content;
					} else {
						html << XMLUtils::HTMLEscape(content);
					}
				}

				// Consuming look-ahead: collect and render inline children at level >= 1
				for (size_t next_idx = block_idx + 1; next_idx < blocks_list.size(); next_idx++) {
					auto &next_block = blocks_list[next_idx];
					if (next_block.IsNull()) {
						continue;
					}
					auto &next_struct = StructValue::GetChildren(next_block);
					std::string next_kind = next_struct[DuckBlockTypes::KIND_IDX].GetValue<string>();
					Value next_level = next_struct[DuckBlockTypes::LEVEL_IDX];

					// Stop if we hit a block or an element back at base level (NULL)
					if (next_kind != DuckBlockTypes::KIND_INLINE) {
						break;
					}
					if (next_level.IsNull() || next_level.GetValue<int32_t>() < 1) {
						break;
					}

					// Render this inline child
					std::string next_type = next_struct[DuckBlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
					std::string next_content = next_struct[DuckBlockTypes::CONTENT_IDX].GetValue<string>();
					auto next_attrs = ExtractAttributes(next_struct[DuckBlockTypes::ATTRIBUTES_IDX]);
					html << RenderInlineElementToHtml(next_type, next_content, next_attrs);

					consumed_indices.insert(next_idx);
				}

				html << "</p>";
			} else if (element_type == DuckBlockTypes::TYPE_CODE) {
				std::string lang_class = "";
				if (attrs.count("language")) {
					lang_class = " class=\"language-" + XMLUtils::HTMLEscape(attrs["language"]) + "\"";
				}
				html << "<pre><code" << lang_class << ">" << XMLUtils::HTMLEscape(content) << "</code></pre>";
			} else if (element_type == DuckBlockTypes::TYPE_BLOCKQUOTE) {
				int depth = level_val.IsNull() ? 1 : level_val.GetValue<int32_t>();
				if (depth < 1)
					depth = 1;
				for (int d = 0; d < depth; d++) {
					html << "<blockquote>";
				}
				if (encoding == DuckBlockTypes::ENCODING_HTML) {
					html << content;
				} else {
					html << XMLUtils::HTMLEscape(content);
				}
				for (int d = 0; d < depth; d++) {
					html << "</blockquote>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_LIST) {
				bool ordered = attrs.count("ordered") && attrs["ordered"] == "true";
				std::string tag = ordered ? "ol" : "ul";
				html << "<" << tag << ">";
				// Parse JSON content as array
				if (encoding == DuckBlockTypes::ENCODING_JSON && !content.empty()) {
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
			} else if (element_type == DuckBlockTypes::TYPE_TABLE) {
				if (encoding == DuckBlockTypes::ENCODING_JSON && !content.empty()) {
					html << TableJsonToHtml(content);
				} else {
					html << "<table></table>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_HR) {
				html << "<hr>";
			} else if (element_type == DuckBlockTypes::TYPE_IMAGE) {
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
			} else if (element_type == DuckBlockTypes::TYPE_RAW) {
				// Pass through raw content
				html << content;
			} else if (element_type == DuckBlockTypes::TYPE_METADATA) {
				// Output as script block with frontmatter MIME type for round-trip preservation
				html << "<script type=\"" << DuckBlockTypes::FRONTMATTER_MIME_TYPE << "\">\n";
				html << content; // No escaping - preserve YAML exactly
				html << "\n</script>";
			}
		}

		result.SetValue(i, Value(html.str()));
	}
}

void DuckBlockFunctions::Register(ExtensionLoader &loader) {
	// html_to_duck_blocks(html HTML) -> LIST(duck_block)
	ScalarFunctionSet html_to_duck_blocks_set("html_to_duck_blocks");
	html_to_duck_blocks_set.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType()}, DuckBlockTypes::DuckBlockListType(), HtmlToDuckBlocksFunction));
	html_to_duck_blocks_set.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR}, DuckBlockTypes::DuckBlockListType(), HtmlToDuckBlocksFunction));
	loader.RegisterFunction(html_to_duck_blocks_set);

	// duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
	auto duck_blocks_to_html_func = ScalarFunction("duck_blocks_to_html", {DuckBlockTypes::DuckBlockListType()},
	                                              XMLTypes::HTMLType(), DuckBlocksToHtmlFunction);
	loader.RegisterFunction(duck_blocks_to_html_func);
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

static std::string UnescapeJsonString(const std::string &str) {
	std::string result;
	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] == '\\' && i + 1 < str.size()) {
			char next = str[i + 1];
			switch (next) {
			case 'n':
				result += '\n';
				break;
			case 'r':
				result += '\r';
				break;
			case 't':
				result += '\t';
				break;
			case '"':
				result += '"';
				break;
			case '\\':
				result += '\\';
				break;
			default:
				result += next;
				break;
			}
			i++;
		} else {
			result += str[i];
		}
	}
	return result;
}

static std::string TableJsonToHtml(const std::string &json) {
	// Parse JSON format: {"headers":["col1","col2"],"rows":[["cell1","cell2"],...]}
	std::stringstream html;
	html << "<table>";

	// Extract headers array
	std::vector<std::string> headers;
	size_t headers_start = json.find("\"headers\":");
	if (headers_start != std::string::npos) {
		size_t arr_start = json.find('[', headers_start);
		size_t arr_end = json.find(']', arr_start);
		if (arr_start != std::string::npos && arr_end != std::string::npos) {
			std::string headers_arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
			std::regex item_regex("\"([^\"\\\\]*(\\\\.[^\"\\\\]*)*)\"");
			std::sregex_iterator iter(headers_arr.begin(), headers_arr.end(), item_regex);
			std::sregex_iterator end;
			while (iter != end) {
				headers.push_back(UnescapeJsonString((*iter)[1].str()));
				++iter;
			}
		}
	}

	// Render headers
	if (!headers.empty()) {
		html << "<thead><tr>";
		for (const auto &h : headers) {
			html << "<th>" << XMLUtils::HTMLEscape(h) << "</th>";
		}
		html << "</tr></thead>";
	}

	// Extract rows array
	size_t rows_start = json.find("\"rows\":");
	if (rows_start != std::string::npos) {
		size_t arr_start = json.find('[', rows_start);
		if (arr_start != std::string::npos) {
			html << "<tbody>";
			// Find each row array [...]
			size_t pos = arr_start + 1;
			while (pos < json.size()) {
				// Skip whitespace
				while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == ',')) {
					pos++;
				}
				if (pos >= json.size() || json[pos] == ']') {
					break;
				}
				if (json[pos] == '[') {
					// Found row start
					size_t row_end = pos + 1;
					int bracket_depth = 1;
					while (row_end < json.size() && bracket_depth > 0) {
						if (json[row_end] == '[') {
							bracket_depth++;
						} else if (json[row_end] == ']') {
							bracket_depth--;
						} else if (json[row_end] == '"') {
							// Skip string content
							row_end++;
							while (row_end < json.size()) {
								if (json[row_end] == '\\' && row_end + 1 < json.size()) {
									row_end += 2;
								} else if (json[row_end] == '"') {
									break;
								} else {
									row_end++;
								}
							}
						}
						row_end++;
					}
					std::string row_str = json.substr(pos + 1, row_end - pos - 2);

					// Extract cells from this row
					std::regex item_regex("\"([^\"\\\\]*(\\\\.[^\"\\\\]*)*)\"");
					std::sregex_iterator iter(row_str.begin(), row_str.end(), item_regex);
					std::sregex_iterator end;
					html << "<tr>";
					while (iter != end) {
						std::string cell = UnescapeJsonString((*iter)[1].str());
						html << "<td>" << XMLUtils::HTMLEscape(cell) << "</td>";
						++iter;
					}
					html << "</tr>";
					pos = row_end;
				} else {
					pos++;
				}
			}
			html << "</tbody>";
		}
	}

	html << "</table>";
	return html.str();
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
