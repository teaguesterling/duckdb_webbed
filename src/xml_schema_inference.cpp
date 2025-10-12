#include "xml_schema_inference.hpp"
#include "xml_types.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <regex>
#include <unordered_set>
#include <libxml/xpath.h>

namespace duckdb {

// Forward declaration for silent error handler from xml_utils.cpp
void XMLSilentErrorHandler(void *ctx, const char *msg, ...);

// Helper: Strip namespace prefix from element name
std::string XMLSchemaInference::StripNamespacePrefix(const std::string &name) {
	size_t colon_pos = name.find(':');
	if (colon_pos != std::string::npos) {
		return name.substr(colon_pos + 1);
	}
	return name;
}

// NEW: Main InferSchema function using 3-phase approach
std::vector<XMLColumnInfo> XMLSchemaInference::InferSchema(const std::string &xml_content,
                                                           const XMLSchemaOptions &options) {
	std::vector<XMLColumnInfo> columns;

	// Parse XML document
	XMLDocRAII doc(xml_content);
	if (!doc.IsValid()) {
		// Fallback
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
		return columns;
	}

	xmlNodePtr root = xmlDocGetRootElement(doc.doc);
	if (!root) {
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
		return columns;
	}

	// Phase 1: Identify Record Elements
	// At max_depth = 0: root is the only record
	// At max_depth >= 1: children of root are records
	std::vector<xmlNodePtr> record_elements = IdentifyRecordElements(doc, root, options);

	if (record_elements.empty()) {
		// No records found
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
		return columns;
	}

	// Calculate effective depth with safety cap
	int effective_depth = options.max_depth;
	if (effective_depth > 20 || effective_depth < 0) {
		effective_depth = 20;
	}

	// Calculate remaining depth for column introspection:
	// max_depth=0: root is record (depth 0), columns at depth 1, remaining = 0-1 = -1 (no depth to introspect)
	// max_depth=1: children are records (depth 1), columns at depth 2, remaining = 1-2 = -1 (no depth to introspect)
	// max_depth=2: children are records (depth 1), columns at depth 2, remaining = 2-2 = 0 (can see columns but nested become XML)
	// max_depth=3: children are records (depth 1), columns at depth 2, remaining = 3-2 = 1 (can introspect 1 level deep)
	int remaining_depth = effective_depth - 2;

	// If remaining_depth < 0, we don't have depth budget to look inside records
	// Return a single column with the record element name, typed as XML
	if (remaining_depth < 0) {
		std::string record_name = (const char *)record_elements[0]->name;
		if (options.namespaces == "strip") {
			record_name = StripNamespacePrefix(record_name);
		}
		columns.emplace_back(record_name, XMLTypes::XMLType(), false, "", 1.0);
		return columns;
	}

	// Phase 2: Identify Columns (immediate children of records)
	auto column_map = IdentifyColumns(record_elements, options);

	// Phase 3: Infer Type for each column
	for (auto &pair : column_map) {
		const auto &col_analysis = pair.second;

		LogicalType col_type = InferColumnType(col_analysis, remaining_depth, options);

		double confidence = static_cast<double>(col_analysis.occurrence_count) / record_elements.size();
		columns.emplace_back(col_analysis.name, col_type, col_analysis.is_attribute, "", confidence);
	}

	// Ensure we have at least some columns
	if (columns.empty()) {
		columns.emplace_back("content", LogicalType::VARCHAR, false, "", 1.0);
	}

	return columns;
}

// Phase 1: Identify Record Elements
std::vector<xmlNodePtr> XMLSchemaInference::IdentifyRecordElements(XMLDocRAII &doc, xmlNodePtr root,
                                                                     const XMLSchemaOptions &options) {
	std::vector<xmlNodePtr> record_elements;

	// Special case: max_depth=0 means root itself is the only record
	int effective_depth = options.max_depth;
	if (effective_depth > 20 || effective_depth < 0) {
		effective_depth = 20; // Safety cap
	}

	if (effective_depth == 0) {
		record_elements.push_back(root);
		return record_elements;
	}

	// If record_element is specified, use XPath to find those elements
	if (!options.record_element.empty()) {
		std::string xpath_expr = options.record_element;

		// If it's a simple tag name (no XPath syntax), convert to namespace-agnostic form
		// This allows "item" to match elements regardless of namespace
		if (xpath_expr.find('/') == std::string::npos &&
		    xpath_expr.find('[') == std::string::npos &&
		    xpath_expr.find('@') == std::string::npos) {
			// Simple tag name - use local-name() for namespace-agnostic matching
			xpath_expr = "//*[local-name()='" + xpath_expr + "']";
		} else if (xpath_expr.substr(0, 2) == "//") {
			// XPath starting with // - check if it's just a simple tag name after //
			std::string tag_part = xpath_expr.substr(2);
			if (tag_part.find('/') == std::string::npos &&
			    tag_part.find('[') == std::string::npos &&
			    tag_part.find('@') == std::string::npos) {
				// Simple tag name after // - use local-name()
				xpath_expr = "//*[local-name()='" + tag_part + "']";
			}
		}

		xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath_expr.c_str(), doc.xpath_ctx);
		xmlSetGenericErrorFunc(nullptr, nullptr);

		if (xpath_obj && xpath_obj->nodesetval) {
			for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
				record_elements.push_back(xpath_obj->nodesetval->nodeTab[i]);
			}
		}
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
	}
	// If a specific root element is specified, find it
	else if (!options.root_element.empty()) {
		std::string xpath = "//" + options.root_element;
		xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);
		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath.c_str(), doc.xpath_ctx);
		xmlSetGenericErrorFunc(nullptr, nullptr);

		if (xpath_obj && xpath_obj->nodesetval && xpath_obj->nodesetval->nodeNr > 0) {
			root = xpath_obj->nodesetval->nodeTab[0];
		}
		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);

		// Collect immediate children of the specified root
		for (xmlNodePtr child = root->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				record_elements.push_back(child);
			}
		}
	}
	// Auto-detect: use deterministic hierarchy (root → rows → columns → nested)
	else {
		// Default behavior: immediate children of root are row-level elements
		for (xmlNodePtr child = root->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				record_elements.push_back(child);
			}
		}
	}

	return record_elements;
}

// Phase 2: Identify Columns
std::unordered_map<std::string, ColumnAnalysis> XMLSchemaInference::IdentifyColumns(
    const std::vector<xmlNodePtr> &record_elements, const XMLSchemaOptions &options) {

	std::unordered_map<std::string, ColumnAnalysis> columns;

	// Iterate through each record element
	for (xmlNodePtr record : record_elements) {
		// Track what columns appear in THIS record
		std::unordered_map<std::string, int> columns_in_this_record;

		// 1. Extract attributes from the record element itself
		if (options.attr_mode != "discard") {
			for (xmlAttrPtr attr = record->properties; attr; attr = attr->next) {
				if (attr->name) {
					std::string attr_name((const char *)attr->name);

					// Apply attribute naming based on attr_mode
					std::string column_name;
					if (options.attr_mode == "prefixed") {
						column_name = options.attr_prefix + attr_name;
					} else {
						// "columns" mode - use attribute name directly
						column_name = attr_name;
					}

					auto it = columns.find(column_name);
					if (it == columns.end()) {
						it = columns.emplace(column_name, ColumnAnalysis(column_name, true)).first;
					}
					auto &col = it->second;
					col.instances.push_back(record); // Store record node for attribute access
					col.occurrence_count++;
					columns_in_this_record[column_name]++;
				}
			}
		}

		// 2. Extract child elements (these become columns)
		// IMPORTANT: Only look at immediate children, do NOT recurse
		for (xmlNodePtr child = record->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				std::string element_name((const char *)child->name);

				// Strip namespace prefix if configured
				if (options.namespaces == "strip") {
					element_name = StripNamespacePrefix(element_name);
				}

				auto it = columns.find(element_name);
				if (it == columns.end()) {
					it = columns.emplace(element_name, ColumnAnalysis(element_name, false)).first;
				}
				auto &col = it->second;
				col.instances.push_back(child);
				col.occurrence_count++;
				columns_in_this_record[element_name]++;
			}
		}

		// Check if any column appeared multiple times in this record
		for (const auto &pair : columns_in_this_record) {
			if (pair.second > 1) {
				auto col_it = columns.find(pair.first);
				if (col_it != columns.end()) {
					col_it->second.repeats_in_record = true;
				}
			}
		}
	}

	return columns;
}

// Helper: Parse force_list XPath string into set of element names
static std::unordered_set<std::string> ParseForceListElements(const std::string &force_list_str) {
	std::unordered_set<std::string> elements;
	if (force_list_str.empty()) {
		return elements;
	}

	// force_list can be:
	// - "//name" - single element
	// - "//name | //item" - multiple elements joined with |
	// Parse each XPath expression and extract the element name
	size_t start = 0;
	while (start < force_list_str.size()) {
		// Find the next | separator or end of string
		size_t end = force_list_str.find('|', start);
		if (end == std::string::npos) {
			end = force_list_str.size();
		}

		// Extract this XPath segment and trim whitespace
		std::string xpath_segment = force_list_str.substr(start, end - start);
		// Trim leading/trailing whitespace
		size_t first = xpath_segment.find_first_not_of(" \t\n\r");
		if (first != std::string::npos) {
			size_t last = xpath_segment.find_last_not_of(" \t\n\r");
			xpath_segment = xpath_segment.substr(first, last - first + 1);

			// Extract element name from XPath (e.g., "//name" -> "name")
			if (xpath_segment.substr(0, 2) == "//") {
				std::string element_name = xpath_segment.substr(2);
				// Remove any further XPath syntax (/, [, @, etc.)
				size_t xpath_end = element_name.find_first_of("/[@");
				if (xpath_end != std::string::npos) {
					element_name = element_name.substr(0, xpath_end);
				}
				if (!element_name.empty()) {
					elements.insert(element_name);
				}
			}
		}

		start = end + 1;
	}

	return elements;
}

// Phase 3: Infer Column Type
LogicalType XMLSchemaInference::InferColumnType(const ColumnAnalysis &column, int remaining_depth,
                                                 const XMLSchemaOptions &options) {

	// Attributes are always VARCHAR (or type-detected if enabled)
	if (column.is_attribute) {
		// TODO: Could sample attribute values for type detection
		return LogicalType::VARCHAR;
	}

	// Check if this column is in force_list (should always be LIST type)
	std::unordered_set<std::string> force_list_elements = ParseForceListElements(options.force_list);
	bool force_as_list = force_list_elements.count(column.name) > 0;

	// Check if all instances are leaf nodes (text only, no children and no attributes)
	// Elements with attributes should be treated as complex (STRUCT), not leaf
	bool all_leaf = true;
	std::vector<std::string> sample_values;

	for (xmlNodePtr node : column.instances) {
		bool is_complex = false;

		// Check for child elements
		for (xmlNodePtr child = node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				is_complex = true;
				break;
			}
		}

		// Check for attributes (unless we're discarding them)
		if (!is_complex && options.attr_mode != "discard") {
			if (node->properties != nullptr) {
				is_complex = true;
			}
		}

		if (is_complex) {
			all_leaf = false;
			break;
		}

		// Collect text content for scalar type detection
		if (sample_values.size() < 20) {
			xmlChar *content = xmlNodeGetContent(node);
			if (content) {
				std::string text = CleanTextContent((const char *)content);
				if (!text.empty()) {
					sample_values.push_back(text);
				}
				xmlFree(content);
			}
		}
	}

	// If all instances are leaf nodes, check if we should force to LIST
	if (all_leaf) {
		LogicalType scalar_type = InferTypeFromSamples(sample_values, options);
		if (force_as_list) {
			// Force this column to be LIST type even though it doesn't repeat
			return LogicalType::LIST(scalar_type);
		}
		return scalar_type;
	}

	// Complex element - check depth limit
	if (remaining_depth <= 0) {
		// Depth limit reached - return XML type
		if (force_as_list) {
			return LogicalType::LIST(XMLTypes::XMLType());
		}
		return XMLTypes::XMLType();
	}

	// Column repeats in a record OR forced to be list → LIST type
	if (column.repeats_in_record || force_as_list) {
		// Analyze the structure of the first instance to determine element type
		xmlNodePtr first = column.instances[0];

		// Check what this element contains
		std::unordered_map<std::string, int> child_counts;
		for (xmlNodePtr child = first->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				std::string child_name((const char *)child->name);
				if (options.namespaces == "strip") {
					child_name = StripNamespacePrefix(child_name);
				}
				child_counts[child_name]++;
			}
		}

		// Build STRUCT type: first attributes, then child elements
		child_list_t<LogicalType> struct_fields;

		// 1. Add attributes as STRUCT fields (unless we're discarding them)
		if (options.attr_mode != "discard" && first->properties != nullptr) {
			for (xmlAttrPtr attr = first->properties; attr; attr = attr->next) {
				if (attr->name) {
					std::string attr_name((const char *)attr->name);
					// Strip namespace prefix if configured
					if (options.namespaces == "strip") {
						attr_name = StripNamespacePrefix(attr_name);
					}
					// Attributes are always VARCHAR
					struct_fields.push_back(make_pair(attr_name, LogicalType::VARCHAR));
				}
			}
		}

		// 2. Add child elements as STRUCT fields (if any)
		if (!child_counts.empty()) {
			std::unordered_set<std::string> seen_children;

			// Iterate over children in document order to preserve field order
			for (xmlNodePtr child = first->children; child; child = child->next) {
				if (child->type == XML_ELEMENT_NODE) {
					std::string child_name((const char *)child->name);
					if (options.namespaces == "strip") {
						child_name = StripNamespacePrefix(child_name);
					}

					// Only process each unique child name once (first occurrence)
					if (seen_children.find(child_name) == seen_children.end()) {
						seen_children.insert(child_name);

						// Create a ColumnAnalysis for this nested child
						ColumnAnalysis nested_col(child_name, false);
						nested_col.instances.push_back(child);
						nested_col.occurrence_count = child_counts[child_name];
						nested_col.repeats_in_record = (child_counts[child_name] > 1);

						// Recursively infer type with decreased depth
						LogicalType child_type = InferColumnType(nested_col, remaining_depth - 1, options);
						struct_fields.push_back(make_pair(child_name, child_type));
					}
				}
			}
		}

		if (!struct_fields.empty()) {
			return LogicalType::LIST(LogicalType::STRUCT(struct_fields));
		}

		// Fallback: LIST<XML>
		return LogicalType::LIST(XMLTypes::XMLType());
	}

	// Single complex element per record → could be STRUCT
	// Analyze its children
	xmlNodePtr first = column.instances[0];
	std::unordered_map<std::string, int> child_counts;

	for (xmlNodePtr child = first->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			std::string child_name((const char *)child->name);
			if (options.namespaces == "strip") {
				child_name = StripNamespacePrefix(child_name);
			}
			child_counts[child_name]++;
		}
	}

	if (!child_counts.empty()) {
		// Build STRUCT type from children IN DOCUMENT ORDER
		child_list_t<LogicalType> struct_fields;
		std::unordered_set<std::string> seen_children;

		// Iterate over children in document order to preserve field order
		for (xmlNodePtr child = first->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				std::string child_name((const char *)child->name);
				if (options.namespaces == "strip") {
					child_name = StripNamespacePrefix(child_name);
				}

				// Only process each unique child name once (first occurrence)
				if (seen_children.find(child_name) == seen_children.end()) {
					seen_children.insert(child_name);

					// Create a ColumnAnalysis for this nested child
					ColumnAnalysis nested_col(child_name, false);
					nested_col.instances.push_back(child);
					nested_col.occurrence_count = child_counts[child_name];
					nested_col.repeats_in_record = (child_counts[child_name] > 1);

					// Recursively infer type with decreased depth
					LogicalType child_type = InferColumnType(nested_col, remaining_depth - 1, options);
					struct_fields.push_back(make_pair(child_name, child_type));
				}
			}
		}

		if (!struct_fields.empty()) {
			return LogicalType::STRUCT(struct_fields);
		}
	}

	// Fallback: XML type
	return XMLTypes::XMLType();
}

std::vector<ElementPattern> XMLSchemaInference::AnalyzeDocumentStructure(const std::string &xml_content,
                                                                         const XMLSchemaOptions &options) {
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

	// Determine which elements to analyze as records
	std::vector<xmlNodePtr> record_elements;

	// If record_element is specified, use XPath to find those elements
	if (!options.record_element.empty()) {
		std::string xpath_expr = options.record_element;

		// If it's a simple tag name (no XPath syntax), convert to namespace-agnostic form
		// This allows "item" to match elements regardless of namespace
		if (xpath_expr.find('/') == std::string::npos &&
		    xpath_expr.find('[') == std::string::npos &&
		    xpath_expr.find('@') == std::string::npos) {
			// Simple tag name - use local-name() for namespace-agnostic matching
			xpath_expr = "//*[local-name()='" + xpath_expr + "']";
		} else if (xpath_expr.substr(0, 2) == "//") {
			// XPath starting with // - check if it's just a simple tag name after //
			std::string tag_part = xpath_expr.substr(2);
			if (tag_part.find('/') == std::string::npos &&
			    tag_part.find('[') == std::string::npos &&
			    tag_part.find('@') == std::string::npos) {
				// Simple tag name after // - use local-name()
				xpath_expr = "//*[local-name()='" + tag_part + "']";
			}
		}

		// Suppress XPath warnings (e.g., undefined namespace prefixes)
		xmlSetGenericErrorFunc(nullptr, XMLSilentErrorHandler);

		xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath_expr.c_str(), xml_doc.xpath_ctx);

		// Restore normal error handling
		xmlSetGenericErrorFunc(nullptr, nullptr);

		if (xpath_obj && xpath_obj->nodesetval) {
			for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
				record_elements.push_back(xpath_obj->nodesetval->nodeTab[i]);
			}
		}

		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);
	}
	// If a specific root element is specified, find it
	else if (!options.root_element.empty()) {
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

		if (xpath_obj)
			xmlXPathFreeObject(xpath_obj);

		// Collect immediate children of the specified root
		for (xmlNodePtr child = root->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				record_elements.push_back(child);
			}
		}
	}
	// Auto-detect: use deterministic hierarchy (root → rows → columns → nested)
	else {
		// Default behavior: immediate children of root are row-level elements
		// This creates a predictable schema where:
		// - Level 0 (root): Container, not data
		// - Level 1 (root's children): Each child becomes one row
		// - Level 2 (grandchildren): Become columns (scalar or arrays if repeating)
		// - Level 3+ (deeper): Become nested types (STRUCT, LIST)
		for (xmlNodePtr child = root->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				record_elements.push_back(child);
			}
		}
	}

	// Analyze the children of each record element (these become columns)
	// With deterministic hierarchy: record elements are rows, their children are columns
	// We need to:
	// 1. Extract attributes from the record element itself (if any)
	// 2. Analyze each child element recursively for nested structure

	// Track record-level attributes separately (they become columns too)
	std::unordered_map<std::string, int32_t> record_attributes;

	for (xmlNodePtr record_node : record_elements) {
		// Check for attributes on the record element itself
		for (xmlAttrPtr attr = record_node->properties; attr; attr = attr->next) {
			if (attr->name) {
				std::string attr_name((const char *)attr->name);
				record_attributes[attr_name]++;
			}
		}

		// Analyze each child element (these become columns)
		for (xmlNodePtr child = record_node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				// Each child will be recursively analyzed to understand nested structure,
				// but only direct children of the record elements will become top-level columns
				AnalyzeElement(child, pattern_map, options);
			}
		}
	}

	// Create a special pattern for record-level attributes if any exist
	if (!record_attributes.empty()) {
		std::string record_element_name = record_elements.empty() ? "record" :
		                                   std::string((const char *)record_elements[0]->name);
		auto &record_pattern = pattern_map[record_element_name];
		record_pattern.name = record_element_name;
		record_pattern.attribute_counts = record_attributes;
		record_pattern.occurrence_count = record_elements.size();
		record_pattern.has_attributes = true;
	}

	// Second pass: compute children_have_attributes and all_children_conforming
	for (auto &pattern_pair : pattern_map) {
		auto &pattern = pattern_pair.second;

		if (pattern.has_children) {
			pattern.children_have_attributes = false;
			pattern.all_children_conforming = true;

			// Check child attributes and consistency for proper tier detection
			XMLTier first_child_tier = XMLTier::FALLBACK_TO_XML; // Use as invalid sentinel
			bool children_have_consistent_tiers = true;
			bool first_child_set = false;

			for (const auto &child_name_count : pattern.child_element_counts) {
				const auto &child_name = child_name_count.first;
				auto child_iter = pattern_map.find(child_name);

				if (child_iter != pattern_map.end()) {
					const auto &child_pattern = child_iter->second;

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
	for (const auto &pair : pattern_map) {
		patterns.push_back(pair.second);
	}

	std::sort(patterns.begin(), patterns.end(),
	          [](const ElementPattern &a, const ElementPattern &b) { return a.occurrence_count > b.occurrence_count; });

	return patterns;
}

void XMLSchemaInference::AnalyzeElement(xmlNodePtr node, std::unordered_map<std::string, ElementPattern> &patterns,
                                        const XMLSchemaOptions &options, int32_t current_depth) {
	if (!node || node->type != XML_ELEMENT_NODE) {
		return;
	}

	// Optional depth limiting for performance (unlimited by default)
	if (options.max_depth >= 0 && current_depth >= options.max_depth) {
		return;
	}

	std::string element_name((const char *)node->name);
	auto &pattern = patterns[element_name];
	pattern.name = element_name;
	pattern.occurrence_count++;

	// Check for text content
	xmlChar *content = xmlNodeGetContent(node);
	if (content) {
		std::string text_content = CleanTextContent((const char *)content);
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
			std::string attr_name((const char *)attr->name);
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
			std::string child_name((const char *)child->name);

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
			for (const auto &child_pair : child_counts) {
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
			for (const auto &child_name : child_names_ordered) {
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
		for (const auto &child_pair : child_counts) {
			if (child_pair.second > 1) {
				pattern.all_children_different_name = false;
				break;
			}
		}

		// We'll compute children_have_attributes and all_children_conforming
		// in a second pass after all patterns are analyzed
	}
}

LogicalType XMLSchemaInference::InferTypeFromSamples(const std::vector<std::string> &samples,
                                                     const XMLSchemaOptions &options) {
	if (samples.empty()) {
		return LogicalType::VARCHAR;
	}

	std::vector<LogicalType> detected_types;

	for (const auto &sample : samples) {
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

LogicalType XMLSchemaInference::InferNestedType(const ElementPattern &pattern,
                                                const std::unordered_map<std::string, ElementPattern> &all_patterns,
                                                const XMLSchemaOptions &options) {

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
			const auto &child_pattern = child_pattern_iter->second;

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

		for (const auto &child_pair : pattern.child_element_counts) {
			const auto &child_name = child_pair.first;
			auto child_pattern_iter = all_patterns.find(child_name);

			if (child_pattern_iter != all_patterns.end()) {
				const auto &child_pattern = child_pattern_iter->second;

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

bool XMLSchemaInference::IsBoolean(const std::string &value) {
	std::string lower = StringUtil::Lower(value);
	return lower == "true" || lower == "false" || lower == "yes" || lower == "no" || lower == "1" || lower == "0" ||
	       lower == "on" || lower == "off";
}

bool XMLSchemaInference::IsInteger(const std::string &value) {
	if (value.empty())
		return false;

	try {
		size_t pos;
		std::stoll(value, &pos);
		return pos == value.length(); // Entire string was converted
	} catch (...) {
		return false;
	}
}

bool XMLSchemaInference::IsDouble(const std::string &value) {
	if (value.empty())
		return false;

	try {
		size_t pos;
		std::stod(value, &pos);
		return pos == value.length(); // Entire string was converted
	} catch (...) {
		return false;
	}
}

bool XMLSchemaInference::IsDate(const std::string &value) {
	// Match common date formats: YYYY-MM-DD, DD/MM/YYYY, MM/DD/YYYY, etc.
	std::regex date_patterns[] = {
	    std::regex(R"(\d{4}-\d{2}-\d{2})"), // YYYY-MM-DD
	    std::regex(R"(\d{2}/\d{2}/\d{4})"), // MM/DD/YYYY or DD/MM/YYYY
	    std::regex(R"(\d{4}/\d{2}/\d{2})"), // YYYY/MM/DD
	    std::regex(R"(\d{2}-\d{2}-\d{4})"), // MM-DD-YYYY or DD-MM-YYYY
	};

	for (const auto &pattern : date_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

bool XMLSchemaInference::IsTime(const std::string &value) {
	// Match time formats: HH:MM:SS, HH:MM, HH:MM:SS.sss
	std::regex time_patterns[] = {
	    std::regex(R"(\d{2}:\d{2}:\d{2})"),      // HH:MM:SS
	    std::regex(R"(\d{2}:\d{2})"),            // HH:MM
	    std::regex(R"(\d{2}:\d{2}:\d{2}\.\d+)"), // HH:MM:SS.sss
	};

	for (const auto &pattern : time_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

bool XMLSchemaInference::IsTimestamp(const std::string &value) {
	// Match ISO timestamp formats and common variations
	std::regex timestamp_patterns[] = {
	    std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})"),      // YYYY-MM-DDTHH:MM:SS
	    std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"),      // YYYY-MM-DD HH:MM:SS
	    std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+)"), // YYYY-MM-DDTHH:MM:SS.sss
	    std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"),     // YYYY-MM-DDTHH:MM:SSZ
	};

	for (const auto &pattern : timestamp_patterns) {
		if (std::regex_match(value, pattern)) {
			return true;
		}
	}
	return false;
}

std::string XMLSchemaInference::GetElementXPath(const std::string &element_name, bool is_attribute,
                                                const std::string &attribute_name) {
	if (is_attribute) {
		return "//" + element_name + "/@" + attribute_name;
	} else {
		return "//" + element_name;
	}
}

LogicalType XMLSchemaInference::GetMostSpecificType(const std::vector<LogicalType> &types) {
	if (types.empty()) {
		return LogicalType::VARCHAR;
	}

	// Count occurrences of each type
	std::unordered_map<LogicalTypeId, int> type_counts;
	for (const auto &type : types) {
		type_counts[type.id()]++;
	}

	// If more than 80% of values are the same type, use that type
	double threshold = 0.8;
	int total = types.size();

	for (const auto &pair : type_counts) {
		if (static_cast<double>(pair.second) / total >= threshold) {
			return LogicalType(pair.first);
		}
	}

	// Fallback: if we have mixed types, prefer VARCHAR for safety
	return LogicalType::VARCHAR;
}

std::string XMLSchemaInference::CleanTextContent(const std::string &text) {
	// Remove leading/trailing whitespace and normalize
	std::string cleaned = text;
	StringUtil::Trim(cleaned);

	// Remove excessive whitespace
	std::regex multiple_spaces(R"(\s+)");
	cleaned = std::regex_replace(cleaned, multiple_spaces, " ");

	return cleaned;
}

std::vector<std::vector<Value>> XMLSchemaInference::ExtractData(const std::string &xml_content,
                                                                const XMLSchemaOptions &options) {
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

	// Use same record identification logic as InferSchema
	std::vector<xmlNodePtr> record_elements = IdentifyRecordElements(doc, root, options);

	// Calculate remaining depth (same logic as InferSchema)
	int effective_depth = options.max_depth;
	if (effective_depth > 20 || effective_depth < 0) {
		effective_depth = 20;
	}
	int remaining_depth = effective_depth - 2;

	// If remaining_depth < 0, serialize each record as XML
	if (remaining_depth < 0) {
		for (xmlNodePtr record : record_elements) {
			std::vector<Value> row;

			// Serialize the record element to XML string
			xmlBufferPtr buffer = xmlBufferCreate();
			if (buffer) {
				xmlNodeDump(buffer, record->doc, record, 0, 1);
				const xmlChar *content = xmlBufferContent(buffer);
				if (content) {
					std::string xml_string((const char *)content);
					row.push_back(Value(xml_string));
				} else {
					row.push_back(Value()); // NULL
				}
				xmlBufferFree(buffer);
			} else {
				row.push_back(Value()); // NULL
			}

			rows.push_back(row);
		}
		return rows;
	}

	// Extract data from each record element
	for (xmlNodePtr current : record_elements) {
		// Extract data for this record
		std::vector<Value> row;

		for (const auto &column : schema) {
			Value value;

				if (column.is_attribute) {
					// Extract attribute value
					std::string attr_name = column.name;
					// Remove element prefix if present (e.g., "employee_id" -> "id")
					size_t underscore_pos = attr_name.find('_');
					if (underscore_pos != std::string::npos) {
						attr_name = attr_name.substr(underscore_pos + 1);
					}

					xmlChar *attr_value = xmlGetProp(current, (const xmlChar *)attr_name.c_str());
					if (attr_value) {
						std::string str_value = (const char *)attr_value;
						value = ConvertToValue(str_value, column.type);
						xmlFree(attr_value);
					} else {
						value = Value(); // NULL
					}
				} else {
					// Extract element text content
					xmlNodePtr child = current->children;
					std::string element_text;

					// Special handling for LIST columns - collect ALL matching children
					if (column.type.id() == LogicalTypeId::LIST) {
						vector<Value> list_values;
						auto element_type = ListType::GetChildType(column.type);

						// Collect all children with matching name
						for (xmlNodePtr child_iter = current->children; child_iter; child_iter = child_iter->next) {
							if (child_iter->type == XML_ELEMENT_NODE &&
							    xmlStrcmp(child_iter->name, (const xmlChar *)column.name.c_str()) == 0) {
								// Extract this list element
								Value element_value = ExtractValueFromNode(child_iter, element_type);
								list_values.push_back(element_value);
							}
						}

						if (!list_values.empty()) {
							value = Value::LIST(element_type, list_values);
						} else {
							value = Value(); // Empty list becomes NULL
						}
						// Skip the rest of the loop since we've handled this column
						row.push_back(value);
						continue;
					}

					while (child) {
						if (child->type == XML_ELEMENT_NODE &&
						    xmlStrcmp(child->name, (const xmlChar *)column.name.c_str()) == 0) {

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
								if (column.type.id() == LogicalTypeId::STRUCT) {
									// Use structured extraction for STRUCT types
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
										for (xmlNodePtr grandchild = child->children; grandchild;
										     grandchild = grandchild->next) {
											if (grandchild->type == XML_ELEMENT_NODE) {
												xmlNodeDump(buffer, grandchild->doc, grandchild, 0, 1);
											}
										}
										// RAII: Copy the content before freeing the buffer
										const xmlChar *content = xmlBufferContent(buffer);
										if (content) {
											element_text = std::string((const char *)content);
										}
										xmlBufferFree(buffer);
									}
								} else {
									// XML: return wrapped content (current behavior)
									xmlBufferPtr buffer = xmlBufferCreate();
									if (buffer) {
										xmlNodeDump(buffer, child->doc, child, 0, 1);
										// RAII: Copy the content before freeing the buffer
										const xmlChar *content = xmlBufferContent(buffer);
										if (content) {
											element_text = std::string((const char *)content);
										}
										xmlBufferFree(buffer);
									}
								}
							} else {
								// Leaf element - return text content
								xmlChar *text_content = xmlNodeGetContent(child);
								if (text_content) {
									element_text = CleanTextContent((const char *)text_content);
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

	return rows;
}

std::vector<std::vector<Value>> XMLSchemaInference::ExtractDataWithSchema(const std::string &xml_content,
                                                                          const std::vector<std::string> &column_names,
                                                                          const std::vector<LogicalType> &column_types,
                                                                          const XMLSchemaOptions &options) {
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
				const auto &column_name = column_names[col_idx];
				const auto &column_type = column_types[col_idx];

				// Find the specific child element or attribute for this column
				Value value;

				// First check if it's an attribute
				xmlChar *attr_value = xmlGetProp(current, (const xmlChar *)column_name.c_str());
				if (attr_value) {
					std::string str_value = (const char *)attr_value;
					value = ConvertToValue(str_value, column_type);
					xmlFree(attr_value);
				} else {
					// Look for child element with matching name
					xmlNodePtr child = current->children;
					while (child) {
						if (child->type == XML_ELEMENT_NODE &&
						    xmlStrcmp(child->name, (const xmlChar *)column_name.c_str()) == 0) {
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

Value XMLSchemaInference::ConvertToValue(const std::string &text, const LogicalType &target_type) {
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
			return Value::TIMESTAMP(Timestamp::FromString(text, false));
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

Value XMLSchemaInference::ExtractValueFromNode(xmlNodePtr node, const LogicalType &target_type) {
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
		xmlChar *text_content = xmlNodeGetContent(node);
		if (text_content) {
			std::string text = CleanTextContent((const char *)text_content);
			xmlFree(text_content);
			return ConvertToValue(text, target_type);
		}
		return Value(); // NULL value
	}
	}
}

Value XMLSchemaInference::ExtractStructFromNode(xmlNodePtr node, const LogicalType &struct_type) {
	if (!node || struct_type.id() != LogicalTypeId::STRUCT) {
		return Value(); // NULL value
	}

	auto &struct_children = StructType::GetChildTypes(struct_type);
	child_list_t<Value> struct_values;

	// Extract each field of the struct
	for (const auto &field : struct_children) {
		const auto &field_name = field.first;
		const auto &field_type = field.second;

		Value field_value;
		bool found = false;

		// First check if this field is an attribute on the node itself
		xmlChar *attr_value = xmlGetProp(node, (const xmlChar *)field_name.c_str());
		if (attr_value) {
			std::string str_value = (const char *)attr_value;
			field_value = ConvertToValue(str_value, field_type);
			xmlFree(attr_value);
			found = true;
		} else {
			// Look for child element with matching name
			xmlNodePtr child = node->children;
			while (child) {
				if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)field_name.c_str()) == 0) {
					// Found matching child element - extract recursively
					field_value = ExtractValueFromNode(child, field_type);
					found = true;
					break;
				}
				child = child->next;
			}
		}

		// If field not found, use NULL
		if (!found) {
			field_value = Value(field_type);
		}

		struct_values.push_back(make_pair(field_name, field_value));
	}

	return Value::STRUCT(struct_values);
}

Value XMLSchemaInference::ExtractListFromNode(xmlNodePtr node, const LogicalType &list_type) {
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
				const xmlChar *content = xmlBufferContent(buffer);
				if (content) {
					std::string xml_string((const char *)content);
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
