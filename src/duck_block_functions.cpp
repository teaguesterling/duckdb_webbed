#include "duck_block_functions.hpp"
#include "duck_block_types.hpp"
#include "duckdb_compat.hpp"
#include "xml_types.hpp"
#include "xml_utils.hpp"
#include "xml_in_memory_reader.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "yyjson.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <sstream>
#include <regex>
#include <set>
#include <mutex>

namespace duckdb {

using namespace duckdb_yyjson;

// Forward declarations for helper functions
static std::string GetNodeTextContent(xmlNodePtr node);
static std::string GetNodeInnerHTML(xmlNodePtr node, xmlDocPtr doc);
static std::string GetNodeAttribute(xmlNodePtr node, const char *attr_name);
static int CountBlockquoteAncestors(xmlNodePtr node);
static std::string ListItemsToJson(xmlNodePtr node);
static std::string TableToJson(xmlNodePtr node);
static std::string TableJsonToHtml(const std::string &json);
static std::string PandocTableToHtml(const std::string &json);
static bool ContentContainsTags(const std::string &content);
static void ConsumeInlineChildren(const vector<Value> &blocks_list, size_t parent_idx,
                                  std::set<size_t> &consumed_indices, std::stringstream &html);

// XPath query for block-level elements
static const char *BLOCK_XPATH = "//body//*[self::h1 or self::h2 or self::h3 or self::h4 or self::h5 or self::h6 "
                                 "or self::p or self::pre or self::blockquote or self::ul or self::ol "
                                 "or self::table or self::hr or self::img or self::figure]";

// XPath query for frontmatter script blocks
static const char *FRONTMATTER_XPATH = "//script[@type='application/vnd.frontmatter+yaml']";

// Helper to read a VARCHAR struct field, treating NULL as an empty string
static std::string GetVarcharField(const Value &val) {
	return val.IsNull() ? std::string() : val.GetValue<string>();
}

// Helper to extract attributes MAP into std::map
static std::map<std::string, std::string> ExtractAttributes(const Value &attrs_val) {
	std::map<std::string, std::string> attrs;
	if (!attrs_val.IsNull()) {
		auto &map_children = MapValue::GetChildren(attrs_val);
		for (auto &entry : map_children) {
			auto &entry_struct = StructValue::GetChildren(entry);
			// Skip entries with NULL key or value - treat the attribute as absent
			if (entry_struct[0].IsNull() || entry_struct[1].IsNull()) {
				continue;
			}
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

// Render the contiguous run of kind='inline' blocks following `parent_idx` as that
// block's inline children, marking each consumed so the main loop skips it.
// Stops at the first non-inline block, or an inline with NULL/<1 level.
static void ConsumeInlineChildren(const vector<Value> &blocks_list, size_t parent_idx,
                                  std::set<size_t> &consumed_indices, std::stringstream &html) {
	for (size_t next_idx = parent_idx + 1; next_idx < blocks_list.size(); next_idx++) {
		auto &next_block = blocks_list[next_idx];
		if (next_block.IsNull()) {
			continue;
		}
		auto &next_struct = StructValue::GetChildren(next_block);
		std::string next_kind = GetVarcharField(next_struct[DuckBlockTypes::KIND_IDX]);
		Value next_level = next_struct[DuckBlockTypes::LEVEL_IDX];

		if (next_kind != DuckBlockTypes::KIND_INLINE) {
			break;
		}
		if (next_level.IsNull() || next_level.GetValue<int32_t>() < 1) {
			break;
		}

		std::string next_type = GetVarcharField(next_struct[DuckBlockTypes::ELEMENT_TYPE_IDX]);
		std::string next_content = GetVarcharField(next_struct[DuckBlockTypes::CONTENT_IDX]);
		auto next_attrs = ExtractAttributes(next_struct[DuckBlockTypes::ATTRIBUTES_IDX]);
		html << RenderInlineElementToHtml(next_type, next_content, next_attrs);

		consumed_indices.insert(next_idx);
	}
}

// True if `node` has at least one child that is an element (not just text),
// excluding non-content script/style/svg/template elements.
static bool HasElementChildren(xmlNodePtr node) {
	for (xmlNodePtr c = node->children; c; c = c->next) {
		if (c->type == XML_ELEMENT_NODE) {
			std::string tag = reinterpret_cast<const char *>(c->name);
			if (tag != "script" && tag != "style" && tag != "noscript" && tag != "template" && tag != "svg") {
				return true;
			}
		}
	}
	return false;
}

// Map an HTML inline tag to a duck_block inline element_type. Returns "" for
// tags that are not a known formatting wrapper.
static std::string InlineTypeForTag(const std::string &tag) {
	if (tag == "strong" || tag == "b")
		return DuckBlockTypes::INLINE_BOLD;
	if (tag == "em" || tag == "i")
		return DuckBlockTypes::INLINE_ITALIC;
	if (tag == "code")
		return DuckBlockTypes::INLINE_CODE;
	if (tag == "del" || tag == "s" || tag == "strike")
		return DuckBlockTypes::INLINE_STRIKETHROUGH;
	if (tag == "sup")
		return DuckBlockTypes::INLINE_SUPERSCRIPT;
	if (tag == "sub")
		return DuckBlockTypes::INLINE_SUBSCRIPT;
	if (tag == "u")
		return DuckBlockTypes::INLINE_UNDERLINE;
	if (tag == "a")
		return DuckBlockTypes::INLINE_LINK;
	if (tag == "span")
		return DuckBlockTypes::INLINE_SPAN;
	return "";
}

// Extract inline elements from an HTML node's children as structured
// kind='inline' duck_blocks. Nested formatting (e.g. <b>x <i>y</i></b>) is
// preserved: a wrapper containing further elements is emitted with empty
// content (=> NULL) followed by its children at level+1; a wrapper containing
// only text becomes a leaf carrying that text. Returns the inline Values and
// advances element_order.
static std::vector<Value> ExtractInlineElements(xmlNodePtr parent_node, int32_t base_level, int32_t &element_order) {
	std::vector<Value> inlines;

	for (xmlNodePtr child = parent_node->children; child; child = child->next) {
		if (child->type == XML_TEXT_NODE) {
			std::string text = reinterpret_cast<const char *>(child->content);
			if (!text.empty()) {
				std::map<std::string, std::string> attrs;
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_TEXT, text,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			}
			continue;
		}
		if (child->type != XML_ELEMENT_NODE) {
			continue;
		}

		std::string tag = reinterpret_cast<const char *>(child->name);
		// Ignore non-content elements that should not leak into text
		if (tag == "script" || tag == "style" || tag == "noscript" || tag == "template" || tag == "svg") {
			continue;
		}

		std::map<std::string, std::string> attrs;

		// Void inline elements (no children to recurse).
		if (tag == "img") {
			std::string src = GetNodeAttribute(child, "src");
			std::string alt = GetNodeAttribute(child, "alt");
			std::string title = GetNodeAttribute(child, "title");
			if (!src.empty())
				attrs["src"] = src;
			if (!alt.empty())
				attrs["alt"] = alt;
			if (!title.empty())
				attrs["title"] = title;
			inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_IMAGE, alt,
			                                               Value::INTEGER(base_level), DuckBlockTypes::ENCODING_TEXT,
			                                               attrs, element_order++));
			continue;
		}
		if (tag == "br") {
			inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_LINEBREAK, "",
			                                               Value::INTEGER(base_level), DuckBlockTypes::ENCODING_TEXT,
			                                               attrs, element_order++));
			continue;
		}

		std::string etype = InlineTypeForTag(tag);
		if (!etype.empty()) {
			if (tag == "a") {
				std::string href = GetNodeAttribute(child, "href");
				if (!href.empty())
					attrs["href"] = href;
				std::string title = GetNodeAttribute(child, "title");
				if (!title.empty())
					attrs["title"] = title;
			} else if (tag == "span") {
				std::string id = GetNodeAttribute(child, "id");
				std::string cls = GetNodeAttribute(child, "class");
				if (!id.empty())
					attrs["id"] = id;
				if (!cls.empty())
					attrs["class"] = cls;
			}
			if (HasElementChildren(child)) {
				// Container: empty content, then recurse children one level deeper.
				inlines.push_back(DuckBlockTypes::CreateInline(etype, "", Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
				auto nested = ExtractInlineElements(child, base_level + 1, element_order);
				inlines.insert(inlines.end(), nested.begin(), nested.end());
			} else {
				// Leaf: carries its text content.
				inlines.push_back(DuckBlockTypes::CreateInline(etype, GetNodeTextContent(child),
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			}
			continue;
		}

		// Unknown inline element: preserve any nested formatting by recursing at
		// the same level (the unknown wrapper is dropped); text-only becomes text.
		if (HasElementChildren(child)) {
			auto nested = ExtractInlineElements(child, base_level, element_order);
			inlines.insert(inlines.end(), nested.begin(), nested.end());
		} else {
			std::string content = GetNodeTextContent(child);
			if (!content.empty()) {
				inlines.push_back(DuckBlockTypes::CreateInline(DuckBlockTypes::INLINE_TEXT, content,
				                                               Value::INTEGER(base_level),
				                                               DuckBlockTypes::ENCODING_TEXT, attrs, element_order++));
			}
		}
	}
	return inlines;
}

vector<Value> DuckBlockFunctions::HtmlToDuckBlocks(const std::string &html_str) {
	if (html_str.empty()) {
		return vector<Value>();
	}

	// Parse HTML via the IO reader so an html value larger than 2 GiB doesn't overflow
	// htmlReadMemory's int length argument (#115), with the fail-closed entity loader and no
	// network (EnsureSecureParsing + HTML_PARSE_NONET, #118).
	XMLUtils::EnsureSecureParsing();
	XMLInMemoryReader reader {html_str.data(), html_str.size(), 0};
	htmlDocPtr doc = htmlReadIO(XMLInMemoryReaderRead, XMLInMemoryReaderClose, &reader, nullptr, "UTF-8",
	                            HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);

	if (!doc) {
		return vector<Value>();
	}

	// Create XPath context
	xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
	if (!xpath_ctx) {
		xmlFreeDoc(doc);
		return vector<Value>();
	}

	vector<Value> blocks;
	int32_t block_order = 0;

	// First, extract frontmatter script blocks
	xmlXPathObjectPtr frontmatter_obj = EvalXPathChecked(xpath_ctx, FRONTMATTER_XPATH);
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
	xmlXPathObjectPtr xpath_obj = EvalXPathChecked(xpath_ctx, BLOCK_XPATH);

	if (xpath_obj && xpath_obj->nodesetval) {
		for (int j = 0; j < xpath_obj->nodesetval->nodeNr; j++) {
			xmlNodePtr node = xpath_obj->nodesetval->nodeTab[j];
			if (!node || !node->name) {
				continue;
			}

			std::string tag(reinterpret_cast<const char *>(node->name));
			std::string content;
			// Top-level blocks are structural depth 1 (matches duckdb_markdown /
			// duck_block_utils); blockquote overrides with its nesting depth below.
			Value level_value = Value::INTEGER(1);
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
				// Check for inline formatting elements
				if (HasElementChildren(node)) {
					// Extract structured inline elements instead of storing raw HTML
					blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_HEADING, "", Value::INTEGER(1),
					                                             DuckBlockTypes::ENCODING_TEXT, attrs, block_order++));
					// Extract inline children at level 2 (parent block is level 1)
					auto inline_elements = ExtractInlineElements(node, 2, block_order);
					blocks.insert(blocks.end(), inline_elements.begin(), inline_elements.end());
					continue; // Skip default block creation
				} else {
					content = GetNodeTextContent(node);
				}
			}
			// Paragraph
			else if (tag == "p") {
				block_type = DuckBlockTypes::TYPE_PARAGRAPH;
				// Check for inline formatting elements
				if (HasElementChildren(node)) {
					// Extract structured inline elements instead of storing raw HTML
					// Create paragraph block with empty content
					blocks.push_back(DuckBlockTypes::CreateBlock(DuckBlockTypes::TYPE_PARAGRAPH, "", Value::INTEGER(1),
					                                             DuckBlockTypes::ENCODING_TEXT, attrs, block_order++));
					// Extract inline children at level 2 (parent block is level 1)
					auto inline_elements = ExtractInlineElements(node, 2, block_order);
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

	return blocks;
}

void DuckBlockFunctions::HtmlToDuckBlocksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto html_value = html_vector.GetValue(i);

		if (html_value.IsNull()) {
			result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		std::string html_str = html_value.GetValue<string>();
		auto blocks = HtmlToDuckBlocks(html_str);
		result.SetValue(i, Value::LIST(DuckBlockTypes::DuckBlockType(), blocks));
	}
}

// A container currently open on the render stack.
struct OpenContainer {
	std::string close_tag;
	int32_t level;
};

// Structural depth, normalised. NULL or anything < 1 is treated as top level,
// matching the previous blockquote behaviour for NULL levels.
static int32_t EffectiveLevel(const Value &level_val) {
	if (level_val.IsNull()) {
		return 1;
	}
	int32_t lvl = level_val.GetValue<int32_t>();
	return lvl < 1 ? 1 : lvl;
}

// Guard against unbounded nesting from caller-constructed block lists. Blocks
// deeper than this render flat rather than pushing, so output stays balanced.
static constexpr int32_t MAX_CONTAINER_DEPTH = 64;

// Map a section block's role to an output tag. The role comes from parsed HTML,
// so it is validated against a fixed enum rather than interpolated: an unchecked
// value would let a crafted document emit a tag it never contained.
static std::string SectionTagForRole(const std::string &role) {
	if (role == "section" || role == "article" || role == "aside" || role == "nav" || role == "header" ||
	    role == "footer" || role == "main") {
		return role;
	}
	return "div";
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
		vector<OpenContainer> open_containers;

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
			// All VARCHAR fields may be NULL (e.g. parent elements with inline children have NULL content)
			std::string kind = GetVarcharField(struct_values[DuckBlockTypes::KIND_IDX]);
			std::string element_type = GetVarcharField(struct_values[DuckBlockTypes::ELEMENT_TYPE_IDX]);
			std::string content = GetVarcharField(struct_values[DuckBlockTypes::CONTENT_IDX]);
			Value level_val = struct_values[DuckBlockTypes::LEVEL_IDX];
			std::string encoding = GetVarcharField(struct_values[DuckBlockTypes::ENCODING_IDX]);
			Value attrs_val = struct_values[DuckBlockTypes::ATTRIBUTES_IDX];

			// Extract attributes from MAP using helper
			std::map<std::string, std::string> attrs = ExtractAttributes(attrs_val);

			// Close every container whose scope this block has left.
			int32_t cur_level = EffectiveLevel(level_val);
			while (!open_containers.empty() && open_containers.back().level >= cur_level) {
				html << open_containers.back().close_tag;
				open_containers.pop_back();
			}

			// Check if this is an inline element
			if (kind == DuckBlockTypes::KIND_INLINE) {
				// Render standalone inline element
				html << RenderInlineElementToHtml(element_type, content, attrs);
				continue;
			}

			// Generate HTML based on block element type
			if (element_type == DuckBlockTypes::TYPE_HEADING) {
				// Read heading level from attributes; non-numeric values fall back to the default
				int lvl = 1;
				if (attrs.count(DuckBlockTypes::ATTR_HEADING_LEVEL)) {
					int32_t parsed_lvl;
					if (TryCast::Operation(string_t(attrs[DuckBlockTypes::ATTR_HEADING_LEVEL]), parsed_lvl, false)) {
						lvl = parsed_lvl;
					}
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

				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);

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

				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);

				html << "</p>";
			} else if (element_type == DuckBlockTypes::TYPE_CODE) {
				std::string lang_class = "";
				if (attrs.count("language")) {
					lang_class = " class=\"language-" + XMLUtils::HTMLEscape(attrs["language"]) + "\"";
				}
				html << "<pre><code" << lang_class << ">" << XMLUtils::HTMLEscape(content) << "</code></pre>";
			} else if (element_type == DuckBlockTypes::TYPE_BLOCKQUOTE) {
				html << "<blockquote>";
				if (!content.empty()) {
					if (encoding == DuckBlockTypes::ENCODING_HTML) {
						html << content;
					} else {
						html << XMLUtils::HTMLEscape(content);
					}
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</blockquote>", cur_level});
				} else {
					html << "</blockquote>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_LIST) {
				bool ordered = attrs.count("ordered") && attrs["ordered"] == "true";
				std::string tag = ordered ? "ol" : "ul";
				html << "<" << tag << ">";
				if (encoding == DuckBlockTypes::ENCODING_JSON && !content.empty()) {
					yyjson_doc *doc = yyjson_read(content.c_str(), content.size(), 0);
					if (doc) {
						yyjson_val *root = yyjson_doc_get_root(doc);
						if (yyjson_is_arr(root)) {
							size_t idx, max;
							yyjson_val *item;
							yyjson_arr_foreach(root, idx, max, item) {
								if (yyjson_is_str(item)) {
									html << "<li>"
									     << XMLUtils::HTMLEscape(
									            std::string(yyjson_get_str(item), yyjson_get_len(item)))
									     << "</li>";
								}
							}
						}
						yyjson_doc_free(doc);
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
			} else if (element_type == DuckBlockTypes::TYPE_SECTION) {
				std::string role = attrs.count(DuckBlockTypes::ATTR_ROLE) ? attrs[DuckBlockTypes::ATTR_ROLE] : "";
				std::string tag = SectionTagForRole(role);
				html << "<" << tag;
				if (attrs.count("id")) {
					html << " id=\"" << XMLUtils::HTMLEscape(attrs["id"]) << "\"";
				}
				if (attrs.count("class")) {
					html << " class=\"" << XMLUtils::HTMLEscape(attrs["class"]) << "\"";
				}
				html << ">";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</" + tag + ">", cur_level});
				} else {
					html << "</" << tag << ">";
				}
			} else if (element_type == DuckBlockTypes::TYPE_FIGURE) {
				html << "<figure>";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</figure>", cur_level});
				} else {
					html << "</figure>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_CAPTION) {
				html << "<figcaption>";
				if (!content.empty()) {
					html << XMLUtils::HTMLEscape(content);
				}
				ConsumeInlineChildren(blocks_list, block_idx, consumed_indices, html);
				if (static_cast<int32_t>(open_containers.size()) < MAX_CONTAINER_DEPTH) {
					open_containers.push_back({"</figcaption>", cur_level});
				} else {
					html << "</figcaption>";
				}
			} else if (element_type == DuckBlockTypes::TYPE_METADATA) {
				// Output as script block with frontmatter MIME type for round-trip preservation
				html << "<script type=\"" << DuckBlockTypes::FRONTMATTER_MIME_TYPE << "\">\n";
				html << content; // No escaping - preserve YAML exactly
				html << "\n</script>";
			}
		}

		// Close anything still open at end of list, so output is always balanced.
		while (!open_containers.empty()) {
			html << open_containers.back().close_tag;
			open_containers.pop_back();
		}

		result.SetValue(i, Value(html.str()));
	}
}

// ============================================================================
// read_html_blocks & parse_html_blocks Table Functions
// ============================================================================

static void ReadFileFully(duckdb::FileHandle &handle, char *data, duckdb::idx_t size) {
	duckdb::idx_t total_read = 0;
	while (total_read < size) {
		auto read_bytes = handle.Read(data + total_read, size - total_read);
		if (read_bytes <= 0) {
			throw duckdb::IOException("Unexpected end of file while reading %s (read %llu of %llu bytes)", handle.path,
			                          total_read, size);
		}
		total_read += read_bytes;
	}
}

struct HTMLBlocksReadFunctionData : public TableFunctionData {
	vector<string> files;
	bool include_filename = false;
	bool ignore_errors = false;
	idx_t max_file_size = 2147483648ULL; // 2GB default
};

struct HTMLBlocksReadGlobalState : public GlobalTableFunctionState {
	vector<string> files;
	std::mutex file_lock;
	idx_t next_file_index = 0;

	idx_t MaxThreads() const override {
		return files.empty() ? 1 : files.size();
	}

	idx_t ClaimNextFile() {
		std::lock_guard<std::mutex> guard(file_lock);
		if (next_file_index >= files.size()) {
			return DConstants::INVALID_INDEX;
		}
		return next_file_index++;
	}
};

struct HTMLBlocksReadLocalState : public LocalTableFunctionState {
	static constexpr idx_t FILE_SHIFT = 32;

	idx_t file_index = DConstants::INVALID_INDEX;
	string current_filename;
	idx_t chunk_counter = 0;
	idx_t last_batch_index = 0;
	bool have_file = false;

	vector<Value> current_blocks;
	idx_t current_block_index = 0;
};

unique_ptr<FunctionData> DuckBlockFunctions::ReadHTMLBlocksBind(ClientContext &context, TableFunctionBindInput &input,
                                                                vector<LogicalType> &return_types,
                                                                vector<string> &names) {
	auto result = make_uniq<HTMLBlocksReadFunctionData>();

	if (input.inputs.empty()) {
		throw InvalidInputException(
		    "read_html_blocks requires at least one argument (file pattern or array of file patterns)");
	}

	vector<string> file_patterns;
	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR) {
		file_patterns.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				throw InvalidInputException("read_html_blocks cannot process NULL file patterns");
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw InvalidInputException("read_html_blocks array parameter must contain only strings");
			}
			file_patterns.push_back(child.ToString());
		}
	} else {
		throw InvalidInputException("read_html_blocks first argument must be a string or array of strings");
	}

	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &pattern : file_patterns) {
		auto glob_result = fs.Glob(pattern, nullptr);
		vector<string> matched;
		for (const auto &file_info : glob_result) {
			matched.push_back(file_info.path);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &matched_path : matched) {
			result->files.push_back(std::move(matched_path));
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
	}

	for (auto &kv : input.named_parameters) {
		if (kv.first == "filename" || kv.first == "file_path" || kv.first == "include_filepath") {
			result->include_filename = kv.second.GetValue<bool>();
		} else if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
		}
	}

	if (result->include_filename) {
		names.push_back("filename");
		return_types.push_back(LogicalType::VARCHAR);
	}

	names.push_back("kind");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("element_type");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("content");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("level");
	return_types.push_back(LogicalType::INTEGER);

	names.push_back("encoding");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("attributes");
	return_types.push_back(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR));

	names.push_back("element_order");
	return_types.push_back(LogicalType::INTEGER);

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> DuckBlockFunctions::ReadHTMLBlocksInit(ClientContext &context,
                                                                            TableFunctionInitInput &input) {
	auto result = make_uniq<HTMLBlocksReadGlobalState>();
	auto &bind_data = input.bind_data->Cast<HTMLBlocksReadFunctionData>();
	result->files = bind_data.files;
	return std::move(result);
}

unique_ptr<LocalTableFunctionState>
DuckBlockFunctions::ReadHTMLBlocksInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                            GlobalTableFunctionState *global_state) {
	return make_uniq<HTMLBlocksReadLocalState>();
}

OperatorPartitionData DuckBlockFunctions::ReadHTMLBlocksGetPartitionData(ClientContext &context,
                                                                         TableFunctionGetPartitionInput &input) {
	auto &lstate = input.local_state->Cast<HTMLBlocksReadLocalState>();
	return OperatorPartitionData(lstate.last_batch_index);
}

void DuckBlockFunctions::ReadHTMLBlocksFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<HTMLBlocksReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<HTMLBlocksReadGlobalState>();
	auto &lstate = data_p.local_state->Cast<HTMLBlocksReadLocalState>();

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;

	while (output_idx < STANDARD_VECTOR_SIZE) {
		if (!lstate.have_file) {
			idx_t claimed = gstate.ClaimNextFile();
			if (claimed == DConstants::INVALID_INDEX) {
				break;
			}
			lstate.file_index = claimed;
			lstate.current_filename = gstate.files[claimed];
			lstate.chunk_counter = 0;
			lstate.have_file = true;
			lstate.current_blocks.clear();
			lstate.current_block_index = 0;

			try {
				auto file_handle = fs.OpenFile(lstate.current_filename, FileFlags::FILE_FLAGS_READ);
				auto file_size = fs.GetFileSize(*file_handle);
				if (file_size > bind_data.max_file_size) {
					if (!bind_data.ignore_errors) {
						throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)",
						                            lstate.current_filename, bind_data.max_file_size);
					}
					lstate.have_file = false;
					continue;
				}
				string content;
				content.resize(file_size);
				ReadFileFully(*file_handle, (char *)content.data(), file_size);
				lstate.current_blocks = HtmlToDuckBlocks(content);
			} catch (const std::exception &e) {
				if (!bind_data.ignore_errors) {
					throw;
				}
				lstate.have_file = false;
				continue;
			}
		}

		// Emit blocks from current file
		while (output_idx < STANDARD_VECTOR_SIZE && lstate.current_block_index < lstate.current_blocks.size()) {
			const auto &block = lstate.current_blocks[lstate.current_block_index];
			auto &children = StructValue::GetChildren(block);

			idx_t col_idx = 0;
			if (bind_data.include_filename) {
				output.data[col_idx++].SetValue(output_idx, Value(lstate.current_filename));
			}
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::KIND_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ELEMENT_TYPE_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::CONTENT_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::LEVEL_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ENCODING_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ATTRIBUTES_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ELEMENT_ORDER_IDX]);

			output_idx++;
			lstate.current_block_index++;
		}

		if (lstate.current_block_index >= lstate.current_blocks.size()) {
			lstate.last_batch_index =
			    (lstate.file_index << HTMLBlocksReadLocalState::FILE_SHIFT) | lstate.chunk_counter++;
			lstate.have_file = false;
			lstate.current_blocks.clear();
			lstate.current_block_index = 0;
		}
	}

	CompatSetOutputCardinality(output, output_idx);
}

struct HTMLBlocksParseFunctionData : public TableFunctionData {
	vector<string> html_contents;
	bool ignore_errors = false;
};

struct HTMLBlocksParseGlobalState : public GlobalTableFunctionState {
	vector<string> html_contents;
	std::mutex content_lock;
	idx_t next_index = 0;

	idx_t MaxThreads() const override {
		return html_contents.empty() ? 1 : html_contents.size();
	}

	idx_t ClaimNext() {
		std::lock_guard<std::mutex> guard(content_lock);
		if (next_index >= html_contents.size()) {
			return DConstants::INVALID_INDEX;
		}
		return next_index++;
	}
};

struct HTMLBlocksParseLocalState : public LocalTableFunctionState {
	bool have_item = false;
	vector<Value> current_blocks;
	idx_t current_block_index = 0;
};

unique_ptr<FunctionData> DuckBlockFunctions::ParseHTMLBlocksBind(ClientContext &context, TableFunctionBindInput &input,
                                                                 vector<LogicalType> &return_types,
                                                                 vector<string> &names) {
	auto result = make_uniq<HTMLBlocksParseFunctionData>();

	if (input.inputs.empty()) {
		throw InvalidInputException("parse_html_blocks requires HTML content as first argument");
	}

	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR || first_input.type() == XMLTypes::HTMLType()) {
		result->html_contents.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (!child.IsNull()) {
				result->html_contents.push_back(child.ToString());
			}
		}
	} else {
		throw InvalidInputException("parse_html_blocks first argument must be a string or array of strings");
	}

	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		}
	}

	names.push_back("kind");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("element_type");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("content");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("level");
	return_types.push_back(LogicalType::INTEGER);

	names.push_back("encoding");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("attributes");
	return_types.push_back(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR));

	names.push_back("element_order");
	return_types.push_back(LogicalType::INTEGER);

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> DuckBlockFunctions::ParseHTMLBlocksInit(ClientContext &context,
                                                                             TableFunctionInitInput &input) {
	auto result = make_uniq<HTMLBlocksParseGlobalState>();
	auto &bind_data = input.bind_data->Cast<HTMLBlocksParseFunctionData>();
	result->html_contents = bind_data.html_contents;
	return std::move(result);
}

unique_ptr<LocalTableFunctionState>
DuckBlockFunctions::ParseHTMLBlocksInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                             GlobalTableFunctionState *global_state) {
	return make_uniq<HTMLBlocksParseLocalState>();
}

void DuckBlockFunctions::ParseHTMLBlocksFunction(ClientContext &context, TableFunctionInput &data_p,
                                                 DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<HTMLBlocksParseFunctionData>();
	auto &gstate = data_p.global_state->Cast<HTMLBlocksParseGlobalState>();
	auto &lstate = data_p.local_state->Cast<HTMLBlocksParseLocalState>();

	idx_t output_idx = 0;

	while (output_idx < STANDARD_VECTOR_SIZE) {
		if (!lstate.have_item) {
			idx_t claimed = gstate.ClaimNext();
			if (claimed == DConstants::INVALID_INDEX) {
				break;
			}
			lstate.have_item = true;
			lstate.current_blocks = HtmlToDuckBlocks(gstate.html_contents[claimed]);
			lstate.current_block_index = 0;
		}

		while (output_idx < STANDARD_VECTOR_SIZE && lstate.current_block_index < lstate.current_blocks.size()) {
			const auto &block = lstate.current_blocks[lstate.current_block_index];
			auto &children = StructValue::GetChildren(block);

			idx_t col_idx = 0;
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::KIND_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ELEMENT_TYPE_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::CONTENT_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::LEVEL_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ENCODING_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ATTRIBUTES_IDX]);
			output.data[col_idx++].SetValue(output_idx, children[DuckBlockTypes::ELEMENT_ORDER_IDX]);

			output_idx++;
			lstate.current_block_index++;
		}

		if (lstate.current_block_index >= lstate.current_blocks.size()) {
			lstate.have_item = false;
			lstate.current_blocks.clear();
			lstate.current_block_index = 0;
		}
	}

	CompatSetOutputCardinality(output, output_idx);
}

void DuckBlockFunctions::Register(ExtensionLoader &loader) {
	// html_to_duck_blocks(html HTML) -> LIST(duck_block)
	ScalarFunctionSet html_to_duck_blocks_set("html_to_duck_blocks");
	html_to_duck_blocks_set.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType()}, DuckBlockTypes::DuckBlockListType(), HtmlToDuckBlocksFunction));
	html_to_duck_blocks_set.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR}, DuckBlockTypes::DuckBlockListType(), HtmlToDuckBlocksFunction));
	PreventStructConstantFolding(html_to_duck_blocks_set);
	loader.RegisterFunction(html_to_duck_blocks_set);

	// duck_blocks_to_html(blocks LIST(duck_block)) -> HTML
	auto duck_blocks_to_html_func = ScalarFunction("duck_blocks_to_html", {DuckBlockTypes::DuckBlockListType()},
	                                               XMLTypes::HTMLType(), DuckBlocksToHtmlFunction);
	loader.RegisterFunction(duck_blocks_to_html_func);

	// read_html_blocks table function
	TableFunctionSet read_html_blocks_set("read_html_blocks");

	TableFunction read_html_blocks_single("read_html_blocks", {LogicalType::VARCHAR}, ReadHTMLBlocksFunction,
	                                      ReadHTMLBlocksBind, ReadHTMLBlocksInit);
	read_html_blocks_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_blocks_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_blocks_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_blocks_single.named_parameters["file_path"] = LogicalType::BOOLEAN;
	read_html_blocks_single.named_parameters["include_filepath"] = LogicalType::BOOLEAN;
	read_html_blocks_single.init_local = ReadHTMLBlocksInitLocal;
	read_html_blocks_single.get_partition_data = ReadHTMLBlocksGetPartitionData;
	read_html_blocks_set.AddFunction(read_html_blocks_single);

	TableFunction read_html_blocks_array("read_html_blocks", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                     ReadHTMLBlocksFunction, ReadHTMLBlocksBind, ReadHTMLBlocksInit);
	read_html_blocks_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_blocks_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_blocks_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_blocks_array.named_parameters["file_path"] = LogicalType::BOOLEAN;
	read_html_blocks_array.named_parameters["include_filepath"] = LogicalType::BOOLEAN;
	read_html_blocks_array.init_local = ReadHTMLBlocksInitLocal;
	read_html_blocks_array.get_partition_data = ReadHTMLBlocksGetPartitionData;
	read_html_blocks_set.AddFunction(read_html_blocks_array);

	loader.RegisterFunction(read_html_blocks_set);

	// parse_html_blocks table function
	TableFunctionSet parse_html_blocks_set("parse_html_blocks");

	TableFunction parse_html_blocks_varchar("parse_html_blocks", {LogicalType::VARCHAR}, ParseHTMLBlocksFunction,
	                                        ParseHTMLBlocksBind, ParseHTMLBlocksInit);
	parse_html_blocks_varchar.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	parse_html_blocks_varchar.init_local = ParseHTMLBlocksInitLocal;
	parse_html_blocks_set.AddFunction(parse_html_blocks_varchar);

	TableFunction parse_html_blocks_html("parse_html_blocks", {XMLTypes::HTMLType()}, ParseHTMLBlocksFunction,
	                                     ParseHTMLBlocksBind, ParseHTMLBlocksInit);
	parse_html_blocks_html.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	parse_html_blocks_html.init_local = ParseHTMLBlocksInitLocal;
	parse_html_blocks_set.AddFunction(parse_html_blocks_html);

	TableFunction parse_html_blocks_varchar_list("parse_html_blocks", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                             ParseHTMLBlocksFunction, ParseHTMLBlocksBind, ParseHTMLBlocksInit);
	parse_html_blocks_varchar_list.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	parse_html_blocks_varchar_list.init_local = ParseHTMLBlocksInitLocal;
	parse_html_blocks_set.AddFunction(parse_html_blocks_varchar_list);

	TableFunction parse_html_blocks_html_list("parse_html_blocks", {LogicalType::LIST(XMLTypes::HTMLType())},
	                                          ParseHTMLBlocksFunction, ParseHTMLBlocksBind, ParseHTMLBlocksInit);
	parse_html_blocks_html_list.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	parse_html_blocks_html_list.init_local = ParseHTMLBlocksInitLocal;
	parse_html_blocks_set.AddFunction(parse_html_blocks_html_list);

	loader.RegisterFunction(parse_html_blocks_set);
}

// ============================================================================
// Helper functions
// ============================================================================

static void AccumulateTextContent(xmlNodePtr node, std::string &result) {
	if (!node) {
		return;
	}
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) {
			if (child->content) {
				result += reinterpret_cast<const char *>(child->content);
			}
		} else if (child->type == XML_ELEMENT_NODE) {
			std::string tag = reinterpret_cast<const char *>(child->name);
			if (tag == "script" || tag == "style" || tag == "noscript" || tag == "template" || tag == "svg") {
				continue;
			}
			AccumulateTextContent(child, result);
		}
	}
}

static std::string GetNodeTextContent(xmlNodePtr node) {
	if (!node) {
		return "";
	}
	if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
		return node->content ? std::string(reinterpret_cast<const char *>(node->content)) : "";
	}
	std::string tag = node->name ? reinterpret_cast<const char *>(node->name) : "";
	if (tag == "script" || tag == "style") {
		xmlChar *content = xmlNodeGetContent(node);
		if (!content) {
			return "";
		}
		std::string result(reinterpret_cast<const char *>(content));
		xmlFree(content);
		return result;
	}
	std::string result;
	AccumulateTextContent(node, result);
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

static std::string RenderPandocInlinesToHtml(yyjson_val *inlines_val, int depth = 0) {
	if (!inlines_val || depth > 100) {
		return "";
	}
	if (yyjson_is_str(inlines_val)) {
		return XMLUtils::HTMLEscape(std::string(yyjson_get_str(inlines_val), yyjson_get_len(inlines_val)));
	}
	if (!yyjson_is_arr(inlines_val)) {
		return "";
	}

	std::stringstream result;
	size_t idx, max;
	yyjson_val *item;
	yyjson_arr_foreach(inlines_val, idx, max, item) {
		if (!yyjson_is_obj(item)) {
			continue;
		}
		yyjson_val *t_val = yyjson_obj_get(item, "t");
		if (!t_val || !yyjson_is_str(t_val)) {
			continue;
		}
		const char *type = yyjson_get_str(t_val);
		yyjson_val *c_val = yyjson_obj_get(item, "c");

		if (strcmp(type, "Str") == 0) {
			if (c_val && yyjson_is_str(c_val)) {
				result << XMLUtils::HTMLEscape(std::string(yyjson_get_str(c_val), yyjson_get_len(c_val)));
			}
		} else if (strcmp(type, "Space") == 0) {
			result << " ";
		} else if (strcmp(type, "SoftBreak") == 0) {
			result << "\n";
		} else if (strcmp(type, "LineBreak") == 0) {
			result << "<br>";
		} else if (strcmp(type, "Strong") == 0) {
			result << "<strong>" << RenderPandocInlinesToHtml(c_val, depth + 1) << "</strong>";
		} else if (strcmp(type, "Emph") == 0) {
			result << "<em>" << RenderPandocInlinesToHtml(c_val, depth + 1) << "</em>";
		} else if (strcmp(type, "Strikeout") == 0) {
			result << "<del>" << RenderPandocInlinesToHtml(c_val, depth + 1) << "</del>";
		} else if (strcmp(type, "Superscript") == 0) {
			result << "<sup>" << RenderPandocInlinesToHtml(c_val, depth + 1) << "</sup>";
		} else if (strcmp(type, "Subscript") == 0) {
			result << "<sub>" << RenderPandocInlinesToHtml(c_val, depth + 1) << "</sub>";
		} else if (strcmp(type, "Code") == 0) {
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *code_val = yyjson_arr_get(c_val, 1);
				if (code_val && yyjson_is_str(code_val)) {
					result << "<code>"
					       << XMLUtils::HTMLEscape(std::string(yyjson_get_str(code_val), yyjson_get_len(code_val)))
					       << "</code>";
				}
			}
		} else if (strcmp(type, "Link") == 0) {
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
				yyjson_val *inlines = yyjson_arr_get(c_val, 1);
				yyjson_val *target = yyjson_arr_get(c_val, 2);
				std::string link_text = RenderPandocInlinesToHtml(inlines, depth + 1);
				std::string url, title;
				if (target && yyjson_is_arr(target)) {
					if (yyjson_arr_size(target) >= 1) {
						yyjson_val *u = yyjson_arr_get(target, 0);
						if (u && yyjson_is_str(u)) {
							url = std::string(yyjson_get_str(u), yyjson_get_len(u));
						}
					}
					if (yyjson_arr_size(target) >= 2) {
						yyjson_val *t = yyjson_arr_get(target, 1);
						if (t && yyjson_is_str(t)) {
							title = std::string(yyjson_get_str(t), yyjson_get_len(t));
						}
					}
				}
				result << "<a href=\"" << XMLUtils::HTMLEscape(url) << "\"";
				if (!title.empty()) {
					result << " title=\"" << XMLUtils::HTMLEscape(title) << "\"";
				}
				result << ">" << link_text << "</a>";
			}
		}
	}
	return result.str();
}

static std::string RenderPandocCellToHtml(yyjson_val *cell_val, int depth = 0) {
	if (!cell_val || depth > 100) {
		return "";
	}
	if (yyjson_is_arr(cell_val)) {
		std::stringstream result;
		size_t idx, max;
		yyjson_val *block;
		yyjson_arr_foreach(cell_val, idx, max, block) {
			if (yyjson_is_obj(block)) {
				yyjson_val *t_val = yyjson_obj_get(block, "t");
				if (t_val && yyjson_is_str(t_val)) {
					const char *t = yyjson_get_str(t_val);
					if (strcmp(t, "Plain") == 0 || strcmp(t, "Para") == 0) {
						yyjson_val *c = yyjson_obj_get(block, "c");
						result << RenderPandocInlinesToHtml(c, depth + 1);
					}
				}
			}
		}
		return result.str();
	}
	return RenderPandocInlinesToHtml(cell_val, depth);
}

static std::string PandocTableToHtml(const std::string &json) {
	std::stringstream html;
	html << "<table>";

	yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
	if (!doc) {
		html << "</table>";
		return html.str();
	}

	yyjson_val *root = yyjson_doc_get_root(doc);
	if (!yyjson_is_arr(root) || yyjson_arr_size(root) < 5) {
		yyjson_doc_free(doc);
		html << "</table>";
		return html.str();
	}

	std::vector<std::string> alignments;
	yyjson_val *aligns_val = yyjson_arr_get(root, 1);
	if (aligns_val && yyjson_is_arr(aligns_val)) {
		size_t idx, max;
		yyjson_val *a;
		yyjson_arr_foreach(aligns_val, idx, max, a) {
			if (yyjson_is_obj(a)) {
				yyjson_val *t = yyjson_obj_get(a, "t");
				if (t && yyjson_is_str(t)) {
					alignments.emplace_back(yyjson_get_str(t), yyjson_get_len(t));
				}
			}
		}
	}

	auto get_align_style = [&](size_t col_idx) -> std::string {
		if (col_idx < alignments.size()) {
			if (alignments[col_idx] == "AlignLeft") {
				return " style=\"text-align: left;\"";
			}
			if (alignments[col_idx] == "AlignRight") {
				return " style=\"text-align: right;\"";
			}
			if (alignments[col_idx] == "AlignCenter") {
				return " style=\"text-align: center;\"";
			}
		}
		return "";
	};

	yyjson_val *headers_val = yyjson_arr_get(root, 3);
	if (headers_val && yyjson_is_arr(headers_val) && yyjson_arr_size(headers_val) > 0) {
		html << "<thead><tr>";
		size_t col_idx = 0;
		size_t idx, max;
		yyjson_val *cell;
		yyjson_arr_foreach(headers_val, idx, max, cell) {
			html << "<th" << get_align_style(col_idx) << ">" << RenderPandocCellToHtml(cell) << "</th>";
			col_idx++;
		}
		html << "</tr></thead>";
	}

	yyjson_val *rows_val = yyjson_arr_get(root, 4);
	if (rows_val && yyjson_is_arr(rows_val)) {
		html << "<tbody>";
		size_t r_idx, r_max;
		yyjson_val *row;
		yyjson_arr_foreach(rows_val, r_idx, r_max, row) {
			if (yyjson_is_arr(row)) {
				html << "<tr>";
				size_t col_idx = 0;
				size_t c_idx, c_max;
				yyjson_val *cell;
				yyjson_arr_foreach(row, c_idx, c_max, cell) {
					html << "<td" << get_align_style(col_idx) << ">" << RenderPandocCellToHtml(cell) << "</td>";
					col_idx++;
				}
				html << "</tr>";
			}
		}
		html << "</tbody>";
	}

	html << "</table>";
	yyjson_doc_free(doc);
	return html.str();
}

static std::string TableJsonToHtml(const std::string &json) {
	size_t first_char = json.find_first_not_of(" \t\n\r");
	if (first_char != std::string::npos && json[first_char] == '[') {
		return PandocTableToHtml(json);
	}

	yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
	if (!doc) {
		return "<table></table>";
	}

	yyjson_val *root = yyjson_doc_get_root(doc);
	if (!yyjson_is_obj(root)) {
		yyjson_doc_free(doc);
		return "<table></table>";
	}

	std::stringstream html;
	html << "<table>";

	yyjson_val *headers_val = yyjson_obj_get(root, "headers");
	if (headers_val && yyjson_is_arr(headers_val) && yyjson_arr_size(headers_val) > 0) {
		html << "<thead><tr>";
		size_t idx, max;
		yyjson_val *h;
		yyjson_arr_foreach(headers_val, idx, max, h) {
			if (yyjson_is_str(h)) {
				html << "<th>" << XMLUtils::HTMLEscape(std::string(yyjson_get_str(h), yyjson_get_len(h))) << "</th>";
			}
		}
		html << "</tr></thead>";
	}

	yyjson_val *rows_val = yyjson_obj_get(root, "rows");
	if (rows_val && yyjson_is_arr(rows_val)) {
		html << "<tbody>";
		size_t r_idx, r_max;
		yyjson_val *row;
		yyjson_arr_foreach(rows_val, r_idx, r_max, row) {
			if (yyjson_is_arr(row)) {
				html << "<tr>";
				size_t c_idx, c_max;
				yyjson_val *cell;
				yyjson_arr_foreach(row, c_idx, c_max, cell) {
					if (yyjson_is_str(cell)) {
						html << "<td>" << XMLUtils::HTMLEscape(std::string(yyjson_get_str(cell), yyjson_get_len(cell)))
						     << "</td>";
					}
				}
				html << "</tr>";
			}
		}
		html << "</tbody>";
	}

	yyjson_val *footers_val = yyjson_obj_get(root, "footers");
	if (footers_val && yyjson_is_arr(footers_val) && yyjson_arr_size(footers_val) > 0) {
		html << "<tfoot>";
		size_t r_idx, r_max;
		yyjson_val *row;
		yyjson_arr_foreach(footers_val, r_idx, r_max, row) {
			if (yyjson_is_arr(row)) {
				html << "<tr>";
				size_t c_idx, c_max;
				yyjson_val *cell;
				yyjson_arr_foreach(row, c_idx, c_max, cell) {
					if (yyjson_is_str(cell)) {
						html << "<td>" << XMLUtils::HTMLEscape(std::string(yyjson_get_str(cell), yyjson_get_len(cell)))
						     << "</td>";
					}
				}
				html << "</tr>";
			}
		}
		html << "</tfoot>";
	}

	html << "</table>";
	yyjson_doc_free(doc);
	return html.str();
}

static std::string ListItemsToJson(xmlNodePtr node) {
	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *arr = yyjson_mut_arr(doc);
	yyjson_mut_doc_set_root(doc, arr);

	xmlNodePtr child = node->children;
	while (child) {
		if (child->type == XML_ELEMENT_NODE && child->name && xmlStrcmp(child->name, BAD_CAST "li") == 0) {
			std::string item_text = GetNodeTextContent(child);
			yyjson_mut_arr_add_strncpy(doc, arr, item_text.data(), item_text.size());
		}
		child = child->next;
	}

	size_t len = 0;
	char *json = yyjson_mut_write(doc, 0, &len);
	std::string res(json ? json : "[]", len);
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static std::string TableToJson(xmlNodePtr node) {
	std::vector<std::string> headers;
	std::vector<std::vector<std::string>> rows;
	std::vector<std::vector<std::string>> footers;

	xmlNodePtr child = node->children;
	while (child) {
		if (child->type == XML_ELEMENT_NODE && child->name) {
			if (xmlStrcmp(child->name, BAD_CAST "thead") == 0) {
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
						break;
					}
					tr = tr->next;
				}
			} else if (xmlStrcmp(child->name, BAD_CAST "tbody") == 0) {
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
							rows.push_back(std::move(row));
						}
					}
					tr = tr->next;
				}
			} else if (xmlStrcmp(child->name, BAD_CAST "tfoot") == 0) {
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
							footers.push_back(std::move(row));
						}
					}
					tr = tr->next;
				}
			} else if (xmlStrcmp(child->name, BAD_CAST "tr") == 0) {
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
						rows.push_back(std::move(row));
					}
				}
			}
		}
		child = child->next;
	}

	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *root = yyjson_mut_obj(doc);
	yyjson_mut_doc_set_root(doc, root);

	yyjson_mut_val *h_arr = yyjson_mut_arr(doc);
	for (const auto &h : headers) {
		yyjson_mut_arr_add_strncpy(doc, h_arr, h.data(), h.size());
	}
	yyjson_mut_obj_add_val(doc, root, "headers", h_arr);

	yyjson_mut_val *r_arr = yyjson_mut_arr(doc);
	for (const auto &r : rows) {
		yyjson_mut_val *row_val = yyjson_mut_arr(doc);
		for (const auto &cell : r) {
			yyjson_mut_arr_add_strncpy(doc, row_val, cell.data(), cell.size());
		}
		yyjson_mut_arr_add_val(r_arr, row_val);
	}
	yyjson_mut_obj_add_val(doc, root, "rows", r_arr);

	if (!footers.empty()) {
		yyjson_mut_val *f_arr = yyjson_mut_arr(doc);
		for (const auto &f : footers) {
			yyjson_mut_val *row_val = yyjson_mut_arr(doc);
			for (const auto &cell : f) {
				yyjson_mut_arr_add_strncpy(doc, row_val, cell.data(), cell.size());
			}
			yyjson_mut_arr_add_val(f_arr, row_val);
		}
		yyjson_mut_obj_add_val(doc, root, "footers", f_arr);
	}

	size_t len = 0;
	char *json = yyjson_mut_write(doc, 0, &len);
	std::string res(json ? json : "{\"headers\":[],\"rows\":[]}", len);
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static bool ContentContainsTags(const std::string &content) {
	// Simple check for HTML tags
	return content.find('<') != std::string::npos && content.find('>') != std::string::npos;
}

} // namespace duckdb
