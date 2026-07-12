#include "xml_scalar_functions.hpp"
#include "xml_utils.hpp"
#include "xml_types.hpp"
#include "duckdb_compat.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/execution/expression_executor.hpp"

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlreader.h>
#include <cctype>
#include <regex>
#include <set>

namespace duckdb {

struct SimpleXMLPath {
	bool absolute = false;
	bool descendant = false;
	bool self = false;
	bool virtual_root = false;
	vector<string> elements;
};

struct SimpleXMLGroup {
	SimpleXMLPath record_path;
	vector<vector<SimpleXMLPath>> fields;
};

static bool IsSimpleElementName(const string &name) {
	if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
		return false;
	}
	for (auto ch : name) {
		if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')) {
			return false;
		}
	}
	return true;
}

static bool ParseSimpleXMLPath(const string &xpath, bool record_path, SimpleXMLPath &result) {
	string remainder;
	if (record_path && xpath == "/") {
		result.virtual_root = true;
		result.absolute = true;
		return true;
	}
	if (!record_path && xpath == ".") {
		result.self = true;
		return true;
	}
	if (!record_path && StringUtil::StartsWith(xpath, ".//")) {
		result.descendant = true;
		remainder = xpath.substr(3);
	} else if (!record_path && StringUtil::StartsWith(xpath, "./")) {
		remainder = xpath.substr(2);
	} else if (!record_path) {
		return false;
	} else if (StringUtil::StartsWith(xpath, "//")) {
		result.descendant = true;
		remainder = xpath.substr(2);
	} else if (StringUtil::StartsWith(xpath, "/")) {
		result.absolute = true;
		remainder = xpath.substr(1);
	} else {
		return false;
	}
	if (remainder.empty()) {
		return false;
	}
	for (auto &element : StringUtil::Split(remainder, '/')) {
		if (!IsSimpleElementName(element)) {
			return false;
		}
		result.elements.push_back(element);
	}
	return !result.elements.empty();
}

static bool CompileSimpleXMLGroups(const vector<string> &record_paths, const vector<vector<vector<string>>> &fields,
                                   vector<SimpleXMLGroup> &result) {
	result.clear();
	for (idx_t group_index = 0; group_index < record_paths.size(); group_index++) {
		SimpleXMLGroup group;
		if (!ParseSimpleXMLPath(record_paths[group_index], true, group.record_path)) {
			result.clear();
			return false;
		}
		for (auto &field : fields[group_index]) {
			vector<SimpleXMLPath> alternatives;
			for (auto &xpath : field) {
				SimpleXMLPath path;
				if (!ParseSimpleXMLPath(xpath, false, path) || (group.record_path.virtual_root && path.self)) {
					result.clear();
					return false;
				}
				alternatives.push_back(std::move(path));
			}
			group.fields.push_back(std::move(alternatives));
		}
		result.push_back(std::move(group));
	}
	return true;
}

struct XMLProjectRecordsBindData : public FunctionData {
	vector<string> record_paths;
	vector<vector<vector<string>>> fields;
	vector<xmlXPathCompExprPtr> compiled_record_paths;
	vector<vector<vector<xmlXPathCompExprPtr>>> compiled_fields;
	vector<SimpleXMLGroup> simple_groups;
	bool supports_fast_path;

	XMLProjectRecordsBindData(vector<string> record_paths_p, vector<vector<vector<string>>> fields_p)
	    : record_paths(std::move(record_paths_p)), fields(std::move(fields_p)) {
		compiled_fields.resize(fields.size());
		try {
			for (auto &record_path : record_paths) {
				auto compiled = xmlXPathCompile(BAD_CAST record_path.c_str());
				if (!compiled) {
					throw BinderException("invalid XML record XPath: %s", record_path);
				}
				compiled_record_paths.push_back(compiled);
			}
			for (idx_t group_index = 0; group_index < fields.size(); group_index++) {
				compiled_fields[group_index].resize(fields[group_index].size());
				for (idx_t field_index = 0; field_index < fields[group_index].size(); field_index++) {
					for (auto &field_path : fields[group_index][field_index]) {
						auto compiled = xmlXPathCompile(BAD_CAST field_path.c_str());
						if (!compiled) {
							throw BinderException("invalid XML field XPath: %s", field_path);
						}
						compiled_fields[group_index][field_index].push_back(compiled);
					}
				}
			}
		} catch (...) {
			FreeCompiledPaths();
			throw;
		}
		supports_fast_path = CompileSimpleXMLGroups(record_paths, fields, simple_groups);
	}

	~XMLProjectRecordsBindData() override {
		FreeCompiledPaths();
	}

	void FreeCompiledPaths() {
		for (auto compiled : compiled_record_paths) {
			xmlXPathFreeCompExpr(compiled);
		}
		compiled_record_paths.clear();
		for (auto &group : compiled_fields) {
			for (auto &field : group) {
				for (auto compiled : field) {
					xmlXPathFreeCompExpr(compiled);
				}
				field.clear();
			}
		}
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<XMLProjectRecordsBindData>(record_paths, fields);
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<XMLProjectRecordsBindData>();
		return record_paths == other.record_paths && fields == other.fields;
	}
};

static vector<string> ReadStringList(const Value &value, const string &name) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value.type()).id() != LogicalTypeId::VARCHAR) {
		throw BinderException("%s must be a non-NULL LIST<VARCHAR>", name);
	}
	vector<string> result;
	for (auto &child : ListValue::GetChildren(value)) {
		if (child.IsNull()) {
			throw BinderException("%s cannot contain NULL values", name);
		}
		result.push_back(StringValue::Get(child));
	}
	return result;
}

static vector<int64_t> ReadCountList(const Value &value, const string &name) {
	if (value.IsNull() || value.type().id() != LogicalTypeId::LIST ||
	    ListType::GetChildType(value.type()).id() != LogicalTypeId::BIGINT) {
		throw BinderException("%s must be a non-NULL LIST<BIGINT>", name);
	}
	vector<int64_t> result;
	for (auto &child : ListValue::GetChildren(value)) {
		if (child.IsNull()) {
			throw BinderException("%s cannot contain NULL values", name);
		}
		result.push_back(child.GetValue<int64_t>());
	}
	return result;
}

static string ExtractFirstNonEmptyText(xmlXPathContextPtr xpath_ctx, xmlNodePtr node,
                                       const vector<xmlXPathCompExprPtr> &alternatives) {
	auto previous_node = xpath_ctx->node;
	xpath_ctx->node = node;
	for (auto xpath : alternatives) {
		xmlXPathObjectPtr xpath_obj = xmlXPathCompiledEval(xpath, xpath_ctx);
		if (!xpath_obj) {
			continue;
		}
		XMLCharPtr content(xmlXPathCastToString(xpath_obj));
		xmlXPathFreeObject(xpath_obj);
		if (content && content.get()[0] != '\0') {
			xpath_ctx->node = previous_node;
			return reinterpret_cast<const char *>(content.get());
		}
	}
	xpath_ctx->node = previous_node;
	return "";
}

struct FastXMLCapture {
	bool seen = false;
	idx_t depth = 0;
	string text;
};

struct FastXMLRecord {
	idx_t group_index;
	idx_t depth;
	vector<vector<FastXMLCapture>> fields;
};

static bool MatchesPath(const SimpleXMLPath &path, const vector<string> &stack, idx_t record_depth) {
	if (path.self) {
		return stack.size() == record_depth;
	}
	idx_t offset = path.absolute ? 0 : record_depth;
	if (stack.size() < offset || stack.size() - offset < path.elements.size()) {
		return false;
	}
	if (!path.descendant && stack.size() - offset != path.elements.size()) {
		return false;
	}
	idx_t start = path.descendant ? stack.size() - path.elements.size() : offset;
	for (idx_t i = 0; i < path.elements.size(); i++) {
		if (stack[start + i] != path.elements[i]) {
			return false;
		}
	}
	return true;
}

static void StartFieldCaptures(FastXMLRecord &record, const SimpleXMLGroup &group, const vector<string> &stack) {
	for (idx_t field_index = 0; field_index < group.fields.size(); field_index++) {
		for (idx_t alternative_index = 0; alternative_index < group.fields[field_index].size(); alternative_index++) {
			auto &capture = record.fields[field_index][alternative_index];
			if (!capture.seen && MatchesPath(group.fields[field_index][alternative_index], stack, record.depth)) {
				capture.seen = true;
				capture.depth = stack.size();
			}
		}
	}
}

static void FinalizeFastRecord(FastXMLRecord &record, vector<vector<vector<string>>> &grouped_records) {
	vector<string> values;
	values.reserve(record.fields.size());
	for (auto &field : record.fields) {
		string value;
		for (auto &alternative : field) {
			if (!alternative.text.empty()) {
				value = alternative.text;
				break;
			}
		}
		values.push_back(std::move(value));
	}
	grouped_records[record.group_index].push_back(std::move(values));
}

static void CloseFastXMLDepth(vector<FastXMLRecord> &active_records, idx_t depth,
                              vector<vector<vector<string>>> &grouped_records) {
	for (auto &record : active_records) {
		for (auto &field : record.fields) {
			for (auto &capture : field) {
				if (capture.depth == depth) {
					capture.depth = 0;
				}
			}
		}
	}
	idx_t index = 0;
	while (index < active_records.size()) {
		if (active_records[index].depth == depth) {
			FinalizeFastRecord(active_records[index], grouped_records);
			active_records.erase(active_records.begin() + index);
		} else {
			index++;
		}
	}
}

enum class FastXMLProjectionResult : uint8_t { SUCCESS, MALFORMED, NEEDS_DOM_NAMESPACE, NEEDS_DOM_DTD_ENTITY };

static bool IsLibxmlResourceError(const xmlError *error) {
	return error && error->code == XML_ERR_NO_MEMORY;
}

[[noreturn]] static void ThrowXMLProjectionOutOfMemory() {
	throw OutOfMemoryException(
	    "Failed to project XML records: libxml2 could not allocate memory (the system may be under memory pressure)");
}

static FastXMLProjectionResult ProjectRecordsFast(const XMLProjectRecordsBindData &spec, const string &xml_string,
                                                  vector<vector<vector<string>>> &grouped_records) {
	XMLUtils::EnsureSecureParsing();
	xmlResetLastError();
	xmlTextReaderPtr reader =
	    xmlReaderForMemory(xml_string.data(), NumericCast<int>(xml_string.size()), nullptr, nullptr,
	                       XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET | XML_PARSE_COMPACT);
	if (!reader) {
		if (IsLibxmlResourceError(xmlGetLastError())) {
			ThrowXMLProjectionOutOfMemory();
		}
		grouped_records.assign(spec.simple_groups.size(), {});
		return FastXMLProjectionResult::MALFORMED;
	}

	vector<string> stack;
	vector<FastXMLRecord> active_records;
	for (idx_t group_index = 0; group_index < spec.simple_groups.size(); group_index++) {
		if (!spec.simple_groups[group_index].record_path.virtual_root) {
			continue;
		}
		FastXMLRecord record {group_index, 0, {}};
		for (auto &field : spec.simple_groups[group_index].fields) {
			record.fields.emplace_back(field.size());
		}
		active_records.push_back(std::move(record));
	}

	int read_status;
	auto result = FastXMLProjectionResult::SUCCESS;
	while ((read_status = xmlTextReaderRead(reader)) == 1) {
		auto node_type = xmlTextReaderNodeType(reader);
		if (node_type == XML_READER_TYPE_DOCUMENT_TYPE || node_type == XML_READER_TYPE_ENTITY_REFERENCE) {
			result = FastXMLProjectionResult::NEEDS_DOM_DTD_ENTITY;
			break;
		}
		if (node_type == XML_READER_TYPE_ELEMENT) {
			auto namespace_uri = xmlTextReaderConstNamespaceUri(reader);
			if (namespace_uri && namespace_uri[0] != '\0') {
				result = FastXMLProjectionResult::NEEDS_DOM_NAMESPACE;
				break;
			}
			auto name = xmlTextReaderConstLocalName(reader);
			if (!name) {
				continue;
			}
			stack.emplace_back(reinterpret_cast<const char *>(name));
			for (auto &record : active_records) {
				StartFieldCaptures(record, spec.simple_groups[record.group_index], stack);
			}
			for (idx_t group_index = 0; group_index < spec.simple_groups.size(); group_index++) {
				auto &group = spec.simple_groups[group_index];
				if (group.record_path.virtual_root || !MatchesPath(group.record_path, stack, 0)) {
					continue;
				}
				FastXMLRecord record {group_index, stack.size(), {}};
				for (auto &field : group.fields) {
					record.fields.emplace_back(field.size());
				}
				StartFieldCaptures(record, group, stack);
				active_records.push_back(std::move(record));
			}
			if (xmlTextReaderIsEmptyElement(reader)) {
				CloseFastXMLDepth(active_records, stack.size(), grouped_records);
				stack.pop_back();
			}
		} else if (node_type == XML_READER_TYPE_TEXT || node_type == XML_READER_TYPE_CDATA ||
		           node_type == XML_READER_TYPE_WHITESPACE || node_type == XML_READER_TYPE_SIGNIFICANT_WHITESPACE) {
			auto value = xmlTextReaderConstValue(reader);
			if (!value) {
				continue;
			}
			for (auto &record : active_records) {
				for (auto &field : record.fields) {
					for (auto &capture : field) {
						if (capture.depth > 0) {
							capture.text += reinterpret_cast<const char *>(value);
						}
					}
				}
			}
		} else if (node_type == XML_READER_TYPE_END_ELEMENT && !stack.empty()) {
			CloseFastXMLDepth(active_records, stack.size(), grouped_records);
			stack.pop_back();
		}
	}
	const bool resource_error = read_status < 0 && IsLibxmlResourceError(xmlGetLastError());
	xmlFreeTextReader(reader);
	if (resource_error) {
		ThrowXMLProjectionOutOfMemory();
	}
	if (result != FastXMLProjectionResult::SUCCESS) {
		grouped_records.assign(spec.simple_groups.size(), {});
		return result;
	}
	if (read_status < 0) {
		grouped_records.assign(spec.simple_groups.size(), {});
		return FastXMLProjectionResult::MALFORMED;
	}
	for (auto &record : active_records) {
		if (record.depth == 0) {
			FinalizeFastRecord(record, grouped_records);
		}
	}
	return FastXMLProjectionResult::SUCCESS;
}

// Iterates a string input vector via UnifiedVectorFormat (handles FLAT, DICTIONARY and CONSTANT
// vectors) and propagates NULL inputs to NULL results. The callback receives the row index and
// the row's string value, and stores its result via result.SetValue.
template <class PROCESS_ROW>
static void ExecuteNullSafeString(Vector &input, Vector &result, idx_t count, PROCESS_ROW process_row) {
	UnifiedVectorFormat input_data;
	CompatToUnifiedFormat(input, count, input_data);
	auto input_strings = UnifiedVectorFormat::GetData<string_t>(input_data);

	for (idx_t i = 0; i < count; i++) {
		auto input_idx = input_data.sel->get_index(i);
		if (!input_data.validity.RowIsValid(input_idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		process_row(i, input_strings[input_idx].GetString());
	}
}

void XMLScalarFunctions::XMLValidFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, bool>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		return XMLUtils::IsValidXML(xml_string);
	});
}

void XMLScalarFunctions::XMLWellFormedFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, bool>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		return XMLUtils::IsWellFormedXML(xml_string);
	});
}

void XMLScalarFunctions::XMLExtractTextFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    xml_vector, xpath_vector, result, args.size(), [&](string_t xml_str, string_t xpath_str) {
		    std::string xml_string = xml_str.GetString();
		    std::string xpath_string = xpath_str.GetString();
		    std::string extracted_text = XMLUtils::ExtractTextByXPath(xml_string, xpath_string);
		    return StringVector::AddString(result, extracted_text);
	    });
}

void XMLScalarFunctions::XMLExtractAllTextFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		// Extract all text content by getting all text nodes and concatenating them
		auto elements = XMLUtils::ExtractByXPath(xml_string, "//text()");
		std::string all_text;
		for (const auto &elem : elements) {
			all_text += elem.text_content;
		}
		return StringVector::AddString(result, all_text);
	});
}

void XMLScalarFunctions::XMLExtractElementsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    xml_vector, xpath_vector, result, args.size(), [&](string_t xml_str, string_t xpath_str) {
		    std::string xml_string = xml_str.GetString();
		    std::string xpath_string = xpath_str.GetString();

		    // Extract XML fragment using our new utility function
		    std::string fragment_xml = XMLUtils::ExtractXMLFragment(xml_string, xpath_string);

		    return StringVector::AddString(result, fragment_xml);
	    });
}

// Returns LIST(VARCHAR) of all matching text content (PostgreSQL-compatible)
void XMLScalarFunctions::XMLExtractTextListFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// A trailing `namespaces` argument (positional or `namespaces := <map/mode>`) arrives as a third
	// column via varargs; delegate to the namespace-aware implementation.
	if (args.ColumnCount() > 2) {
		XMLExtractTextListWithNamespacesFunction(args, state, result);
		return;
	}
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();

	// Use UnifiedVectorFormat to handle different vector types (FLAT, DICTIONARY, CONSTANT, etc.)
	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		// Handle NULL values
		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(LogicalType::VARCHAR, vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Return LIST of all matches
		auto texts = XMLUtils::ExtractAllTextByXPath(xml_string, xpath_string);
		vector<Value> text_values;
		for (const auto &text : texts) {
			text_values.push_back(Value(text));
		}
		result.SetValue(i, Value::LIST(LogicalType::VARCHAR, text_values));
	}
}

unique_ptr<FunctionData> XMLScalarFunctions::XMLProjectRecordsBind(DUCKDB_SCALAR_BIND_PARAMS) {
	auto &bind_args = DUCKDB_SCALAR_BIND_ARGS;
	auto &bind_ctx = DUCKDB_SCALAR_BIND_CONTEXT;
	if (bind_args.size() != 5) {
		throw BinderException("xml_project_records requires five arguments");
	}

	vector<Value> values;
	values.reserve(4);
	for (idx_t i = 1; i < bind_args.size(); i++) {
		if (bind_args[i]->HasParameter()) {
			throw ParameterNotResolvedException();
		}
		if (!bind_args[i]->IsFoldable()) {
			throw BinderException("xml_project_records specification arguments must be constant");
		}
		values.push_back(ExpressionExecutor::EvaluateScalar(bind_ctx, *bind_args[i]));
	}

	auto record_paths = ReadStringList(values[0], "record_paths");
	auto field_counts = ReadCountList(values[1], "field_counts");
	auto alternative_counts = ReadCountList(values[2], "alternative_counts");
	auto field_paths = ReadStringList(values[3], "field_paths");
	if (record_paths.empty() || record_paths.size() != field_counts.size()) {
		throw BinderException("record_paths and field_counts must have the same non-zero length");
	}

	vector<vector<vector<string>>> fields(record_paths.size());
	idx_t field_index = 0;
	idx_t path_index = 0;
	for (idx_t group_index = 0; group_index < record_paths.size(); group_index++) {
		auto field_count = field_counts[group_index];
		if (field_count < 0) {
			throw BinderException("field_counts cannot contain negative values");
		}
		if (static_cast<uint64_t>(field_count) > alternative_counts.size() - field_index) {
			throw BinderException("sum(field_counts) must equal len(alternative_counts)");
		}
		auto group_field_end = field_index + static_cast<idx_t>(field_count);
		for (; field_index < group_field_end; field_index++) {
			auto alternative_count = alternative_counts[field_index];
			if (alternative_count <= 0) {
				throw BinderException("alternative_counts must contain positive values");
			}
			if (static_cast<uint64_t>(alternative_count) > field_paths.size() - path_index) {
				throw BinderException("sum(alternative_counts) must equal len(field_paths)");
			}
			auto field_path_end = path_index + static_cast<idx_t>(alternative_count);
			vector<string> alternatives;
			for (; path_index < field_path_end; path_index++) {
				alternatives.push_back(field_paths[path_index]);
			}
			fields[group_index].push_back(std::move(alternatives));
		}
	}
	if (field_index != alternative_counts.size()) {
		throw BinderException("sum(field_counts) must equal len(alternative_counts)");
	}
	if (path_index != field_paths.size()) {
		throw BinderException("sum(alternative_counts) must equal len(field_paths)");
	}
	return make_uniq<XMLProjectRecordsBindData>(std::move(record_paths), std::move(fields));
}

static XMLProjectRecordsBindData &GetXMLProjectRecordsBindData(ExpressionState &state) {
	auto &func_expr = state.expr.Cast<BoundFunctionExpression>();
#ifdef DUCKDB_HAS_NEW_VECTOR_HEADERS
	auto &bind_info = func_expr.BindInfo();
#else
	auto &bind_info = func_expr.bind_info;
#endif
	if (!bind_info) {
		throw InternalException("xml_project_records bind data is missing");
	}
	return bind_info->Cast<XMLProjectRecordsBindData>();
}

static void ProjectRecordsDOM(const XMLProjectRecordsBindData &spec, const string &xml_string,
                              vector<vector<vector<string>>> &grouped_records) {
	XMLDocRAII xml_doc(xml_string);
	if (!xml_doc.IsValid()) {
		if (xml_doc.HadResourceError()) {
			ThrowXMLProjectionOutOfMemory();
		}
		return;
	}
	if (!xml_doc.xpath_ctx) {
		ThrowXMLProjectionOutOfMemory();
	}
	for (idx_t group_index = 0; group_index < spec.record_paths.size(); group_index++) {
		xml_doc.xpath_ctx->node = xmlDocGetRootElement(xml_doc.doc);
		xmlXPathObjectPtr group_obj = xmlXPathCompiledEval(spec.compiled_record_paths[group_index], xml_doc.xpath_ctx);
		if (!group_obj || !group_obj->nodesetval) {
			if (group_obj) {
				xmlXPathFreeObject(group_obj);
			}
			continue;
		}
		for (int node_index = 0; node_index < group_obj->nodesetval->nodeNr; node_index++) {
			auto node = group_obj->nodesetval->nodeTab[node_index];
			if (!node) {
				continue;
			}
			vector<string> values;
			values.reserve(spec.fields[group_index].size());
			for (auto &alternatives : spec.compiled_fields[group_index]) {
				values.emplace_back(ExtractFirstNonEmptyText(xml_doc.xpath_ctx, node, alternatives));
			}
			grouped_records[group_index].push_back(std::move(values));
		}
		xmlXPathFreeObject(group_obj);
	}
}

static void AppendProjectedRecordValues(Vector &result, idx_t row_index,
                                        const vector<vector<vector<string>>> &grouped_records) {
	idx_t appended_record_count = 0;
	idx_t appended_value_count = 0;
	for (auto &group : grouped_records) {
		appended_record_count += group.size();
		for (auto &record : group) {
			appended_value_count += record.size();
		}
	}

	auto record_offset = ListVector::GetListSize(result);
	auto value_offset = ListVector::GetListSize(CompatStructGetField(CompatListGetChild(result), 1));
	ListVector::Reserve(result, record_offset + appended_record_count);
	auto &record_vector = CompatListGetChild(result);
	record_vector.SetVectorType(VectorType::FLAT_VECTOR);
	auto &group_index_vector = CompatStructGetField(record_vector, 0);
	auto &values_vector = CompatStructGetField(record_vector, 1);
	group_index_vector.SetVectorType(VectorType::FLAT_VECTOR);
	values_vector.SetVectorType(VectorType::FLAT_VECTOR);
	ListVector::Reserve(values_vector, value_offset + appended_value_count);
	auto &string_vector = CompatListGetChild(values_vector);
	string_vector.SetVectorType(VectorType::FLAT_VECTOR);

	auto row_entries = FlatVector::GetData<list_entry_t>(result);
	auto group_indices = FlatVector::GetData<int64_t>(group_index_vector);
	auto value_entries = FlatVector::GetData<list_entry_t>(values_vector);
	auto strings = FlatVector::GetData<string_t>(string_vector);
	row_entries[row_index].offset = record_offset;
	row_entries[row_index].length = appended_record_count;
	FlatVector::Validity(result).SetValid(row_index);

	auto record_index = record_offset;
	auto value_index = value_offset;
	for (idx_t group_index = 0; group_index < grouped_records.size(); group_index++) {
		for (auto &record : grouped_records[group_index]) {
			group_indices[record_index] = NumericCast<int64_t>(group_index);
			FlatVector::Validity(group_index_vector).SetValid(record_index);
			value_entries[record_index].offset = value_index;
			value_entries[record_index].length = record.size();
			FlatVector::Validity(values_vector).SetValid(record_index);
			for (auto &value : record) {
				strings[value_index] = StringVector::AddString(string_vector, value);
				FlatVector::Validity(string_vector).SetValid(value_index);
				value_index++;
			}
			record_index++;
		}
	}
	ListVector::SetListSize(values_vector, value_index);
	ListVector::SetListSize(result, record_index);
}

enum class XMLProjectionMode : uint8_t { AUTO, DOM, SAX };

static void ExecuteXMLProjectRecords(DataChunk &args, ExpressionState &state, Vector &result, XMLProjectionMode mode) {
	auto &spec = GetXMLProjectRecordsBindData(state);
	result.SetVectorType(VectorType::FLAT_VECTOR);
	ListVector::SetListSize(result, 0);
	auto &values_vector = CompatStructGetField(CompatListGetChild(result), 1);
	ListVector::SetListSize(values_vector, 0);

	UnifiedVectorFormat input_data;
	CompatToUnifiedFormat(args.data[0], args.size(), input_data);
	auto input_strings = UnifiedVectorFormat::GetData<string_t>(input_data);
	for (idx_t row_index = 0; row_index < args.size(); row_index++) {
		auto input_index = input_data.sel->get_index(row_index);
		if (!input_data.validity.RowIsValid(input_index)) {
			auto row_entries = FlatVector::GetData<list_entry_t>(result);
			row_entries[row_index].offset = ListVector::GetListSize(result);
			row_entries[row_index].length = 0;
			FlatVector::Validity(result).SetInvalid(row_index);
			continue;
		}
		auto xml_string = input_strings[input_index].GetString();
		vector<vector<vector<string>>> grouped_records(spec.record_paths.size());
		bool projected = false;
		if (mode != XMLProjectionMode::DOM && spec.supports_fast_path) {
			auto fast_result = ProjectRecordsFast(spec, xml_string, grouped_records);
			projected = fast_result == FastXMLProjectionResult::SUCCESS ||
			            fast_result == FastXMLProjectionResult::MALFORMED;
			if (!projected && mode == XMLProjectionMode::SAX) {
				if (fast_result == FastXMLProjectionResult::NEEDS_DOM_DTD_ENTITY) {
					throw InvalidInputException(
					    "xml_project_records_sax does not support DTD or entity-reference input");
				}
				throw InvalidInputException("xml_project_records_sax does not support namespaced XML");
			}
		} else if (mode == XMLProjectionMode::SAX) {
			throw InvalidInputException("xml_project_records_sax does not support this XPath projection");
		}
		if (!projected) {
			grouped_records.assign(spec.record_paths.size(), {});
			ProjectRecordsDOM(spec, xml_string, grouped_records);
		}
		AppendProjectedRecordValues(result, row_index, grouped_records);
	}
}

void XMLScalarFunctions::XMLProjectRecordsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteXMLProjectRecords(args, state, result, XMLProjectionMode::AUTO);
}

void XMLScalarFunctions::XMLProjectRecordsDOMFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteXMLProjectRecords(args, state, result, XMLProjectionMode::DOM);
}

void XMLScalarFunctions::XMLProjectRecordsSAXFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteXMLProjectRecords(args, state, result, XMLProjectionMode::SAX);
}

// Returns LIST(VARCHAR) with custom namespace mappings
void XMLScalarFunctions::XMLExtractTextListWithNamespacesFunction(DataChunk &args, ExpressionState &state,
                                                                  Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto &ns_vector = args.data[2];
	auto count = args.size();

	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(LogicalType::VARCHAR, vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Parse namespace parameter
		Value ns_value = ns_vector.GetValue(i);
		NamespaceConfig ns_config = ParseNamespacesParam(ns_value);

		// Return LIST of all matches using NamespaceConfig (handles AUTO mode transformation)
		auto texts = XMLUtils::ExtractAllTextByXPath(xml_string, xpath_string, ns_config);
		vector<Value> text_values;
		for (const auto &text : texts) {
			text_values.push_back(Value(text));
		}
		result.SetValue(i, Value::LIST(LogicalType::VARCHAR, text_values));
	}
}

// Returns LIST(XMLFragment) of all matching elements (PostgreSQL-compatible)
void XMLScalarFunctions::XMLExtractElementsListFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// A trailing `namespaces` argument (positional or `namespaces := <map/mode>`) arrives as a third
	// column via varargs; delegate to the namespace-aware implementation.
	if (args.ColumnCount() > 2) {
		XMLExtractElementsListWithNamespacesFunction(args, state, result);
		return;
	}
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();

	// Use UnifiedVectorFormat to handle different vector types (FLAT, DICTIONARY, CONSTANT, etc.)
	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		// Handle NULL values
		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(XMLTypes::XMLFragmentType(), vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Return LIST of all matching elements
		auto fragments = XMLUtils::ExtractXMLFragmentList(xml_string, xpath_string);
		vector<Value> fragment_values;
		for (const auto &fragment : fragments) {
			fragment_values.push_back(Value(fragment));
		}
		result.SetValue(i, Value::LIST(XMLTypes::XMLFragmentType(), fragment_values));
	}
}

// Returns LIST(XMLFragment) with custom namespace mappings
void XMLScalarFunctions::XMLExtractElementsListWithNamespacesFunction(DataChunk &args, ExpressionState &state,
                                                                      Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto &ns_vector = args.data[2];
	auto count = args.size();

	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(XMLTypes::XMLFragmentType(), vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Parse namespace parameter
		Value ns_value = ns_vector.GetValue(i);
		NamespaceConfig ns_config = ParseNamespacesParam(ns_value);

		// Return LIST of all matching elements with namespace config (handles AUTO mode)
		auto fragments = XMLUtils::ExtractXMLFragmentList(xml_string, xpath_string, ns_config);
		vector<Value> fragment_values;
		for (const auto &fragment : fragments) {
			fragment_values.push_back(Value(fragment));
		}
		result.SetValue(i, Value::LIST(XMLTypes::XMLFragmentType(), fragment_values));
	}
}

void XMLScalarFunctions::XMLExtractElementsStringFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// A trailing `namespaces` argument (positional or `namespaces := <map/mode>`) arrives as a third
	// column via varargs; delegate to the namespace-aware implementation.
	if (args.ColumnCount() > 2) {
		XMLExtractElementsStringWithNamespacesFunction(args, state, result);
		return;
	}
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    xml_vector, xpath_vector, result, args.size(), [&](string_t xml_str, string_t xpath_str) {
		    std::string xml_string = xml_str.GetString();
		    std::string xpath_string = xpath_str.GetString();

		    // Extract ALL XML fragments separated by newlines
		    std::string fragment_xml = XMLUtils::ExtractXMLFragmentAll(xml_string, xpath_string);

		    return StringVector::AddString(result, fragment_xml);
	    });
}

// Returns elements as string with custom namespace mappings
void XMLScalarFunctions::XMLExtractElementsStringWithNamespacesFunction(DataChunk &args, ExpressionState &state,
                                                                        Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto &ns_vector = args.data[2];
	auto count = args.size();

	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value(""));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Parse namespace parameter
		Value ns_value = ns_vector.GetValue(i);
		NamespaceConfig ns_config = ParseNamespacesParam(ns_value);

		// Extract with namespace config (handles AUTO mode)
		std::string fragment_xml = XMLUtils::ExtractXMLFragmentAll(xml_string, xpath_string, ns_config);
		result.SetValue(i, Value(fragment_xml));
	}
}

void XMLScalarFunctions::XMLWrapFragmentFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &fragment_vector = args.data[0];
	auto &wrapper_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    fragment_vector, wrapper_vector, result, args.size(), [&](string_t fragment_str, string_t wrapper_str) {
		    std::string fragment = fragment_str.GetString();
		    std::string wrapper = wrapper_str.GetString();

		    // Reject wrapper names that are not valid XML element names to prevent markup injection.
		    // The embedded NUL check guards against names that truncate during C-string validation.
		    if (wrapper.find('\0') != std::string::npos ||
		        xmlValidateName(reinterpret_cast<const xmlChar *>(wrapper.c_str()), 0) != 0) {
			    throw InvalidInputException("xml_wrap_fragment: '%s' is not a valid XML element name", wrapper);
		    }

		    // Create wrapped XML: <wrapper>fragment</wrapper>
		    std::string wrapped_xml = "<" + wrapper + ">" + fragment + "</" + wrapper + ">";

		    return StringVector::AddString(result, wrapped_xml);
	    });
}

void XMLScalarFunctions::XMLExtractAttributesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// A trailing `namespaces` argument (positional or `namespaces := <map/mode>`) arrives as a third
	// column via varargs; delegate to the namespace-aware implementation.
	if (args.ColumnCount() > 2) {
		XMLExtractAttributesWithNamespacesFunction(args, state, result);
		return;
	}
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();

	// Use UnifiedVectorFormat to handle different vector types (FLAT, DICTIONARY, CONSTANT, etc.)
	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	// Define the struct type for attributes upfront
	auto attr_struct_type = LogicalType::STRUCT(
	    {make_pair("element_name", LogicalType::VARCHAR), make_pair("element_path", LogicalType::VARCHAR),
	     make_pair("attribute_name", LogicalType::VARCHAR), make_pair("attribute_value", LogicalType::VARCHAR),
	     make_pair("line_number", LogicalType::BIGINT)});

	// Extract attributes from elements matching XPath expression
	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		// Handle NULL values
		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(attr_struct_type, vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Extract elements and their attributes using XPath
		auto elements = XMLUtils::ExtractByXPath(xml_string, xpath_string);

		// Create list of attribute structs
		vector<Value> attr_values;

		for (const auto &elem : elements) {
			// For each element, extract its attributes
			for (const auto &attr_pair : elem.attributes) {
				child_list_t<Value> attr_children;
				attr_children.emplace_back("element_name", Value(elem.name));
				attr_children.emplace_back("element_path", Value(elem.path));
				attr_children.emplace_back("attribute_name", Value(attr_pair.first));
				attr_children.emplace_back("attribute_value", Value(attr_pair.second));
				attr_children.emplace_back("line_number", Value::BIGINT(elem.line_number));

				attr_values.emplace_back(Value::STRUCT(attr_children));
			}
		}

		// Set result
		result.SetValue(i, Value::LIST(attr_struct_type, attr_values));
	}
}

// Returns LIST<STRUCT> of attributes with custom namespace mappings
void XMLScalarFunctions::XMLExtractAttributesWithNamespacesFunction(DataChunk &args, ExpressionState &state,
                                                                    Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto &ns_vector = args.data[2];
	auto count = args.size();

	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	// Define the struct type for attributes upfront
	auto attr_struct_type = LogicalType::STRUCT(
	    {make_pair("element_name", LogicalType::VARCHAR), make_pair("element_path", LogicalType::VARCHAR),
	     make_pair("attribute_name", LogicalType::VARCHAR), make_pair("attribute_value", LogicalType::VARCHAR),
	     make_pair("line_number", LogicalType::BIGINT)});

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(attr_struct_type, vector<Value>()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Parse namespace parameter
		Value ns_value = ns_vector.GetValue(i);
		NamespaceConfig ns_config = ParseNamespacesParam(ns_value);

		// Extract elements and their attributes using XPath with namespace config (handles AUTO mode)
		auto elements = XMLUtils::ExtractByXPath(xml_string, xpath_string, ns_config);

		// Create list of attribute structs
		vector<Value> attr_values;

		for (const auto &elem : elements) {
			for (const auto &attr_pair : elem.attributes) {
				child_list_t<Value> attr_children;
				attr_children.emplace_back("element_name", Value(elem.name));
				attr_children.emplace_back("element_path", Value(elem.path));
				attr_children.emplace_back("attribute_name", Value(attr_pair.first));
				attr_children.emplace_back("attribute_value", Value(attr_pair.second));
				attr_children.emplace_back("line_number", Value::BIGINT(elem.line_number));

				attr_values.emplace_back(Value::STRUCT(attr_children));
			}
		}

		result.SetValue(i, Value::LIST(attr_struct_type, attr_values));
	}
}

void XMLScalarFunctions::XMLPrettyPrintFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		std::string formatted = XMLUtils::PrettyPrintXML(xml_string);
		return StringVector::AddString(result, formatted);
	});
}

void XMLScalarFunctions::XMLMinifyFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		std::string minified = XMLUtils::MinifyXML(xml_string);
		return StringVector::AddString(result, minified);
	});
}

void XMLScalarFunctions::XMLValidateSchemaFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto &schema_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, bool>(xml_vector, schema_vector, result, args.size(),
	                                                  [&](string_t xml_str, string_t schema_str) {
		                                                  std::string xml_string = xml_str.GetString();
		                                                  std::string schema_string = schema_str.GetString();
		                                                  return XMLUtils::ValidateXMLSchema(xml_string, schema_string);
	                                                  });
}

void XMLScalarFunctions::XMLExtractCommentsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(xml_vector, result, count, [&](idx_t i, const std::string &xml_string) {
		auto comments = XMLUtils::ExtractComments(xml_string);

		// Create list of comment structs
		vector<Value> comment_values;

		for (const auto &comment : comments) {
			child_list_t<Value> comment_children;
			comment_children.emplace_back("content", Value(comment.content));
			comment_children.emplace_back("line_number", Value::BIGINT(comment.line_number));

			comment_values.emplace_back(Value::STRUCT(comment_children));
		}

		// Create list value
		auto comment_struct_type = LogicalType::STRUCT(
		    {make_pair("content", LogicalType::VARCHAR), make_pair("line_number", LogicalType::BIGINT)});

		Value list_value = Value::LIST(comment_struct_type, comment_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::XMLExtractCDataFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(xml_vector, result, count, [&](idx_t i, const std::string &xml_string) {
		auto cdata_sections = XMLUtils::ExtractCData(xml_string);

		// Create list of CDATA structs
		vector<Value> cdata_values;

		for (const auto &cdata : cdata_sections) {
			child_list_t<Value> cdata_children;
			cdata_children.emplace_back("content", Value(cdata.content));
			cdata_children.emplace_back("line_number", Value::BIGINT(cdata.line_number));

			cdata_values.emplace_back(Value::STRUCT(cdata_children));
		}

		// Create list value
		auto cdata_struct_type = LogicalType::STRUCT(
		    {make_pair("content", LogicalType::VARCHAR), make_pair("line_number", LogicalType::BIGINT)});

		Value list_value = Value::LIST(cdata_struct_type, cdata_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(xml_vector, result, count, [&](idx_t i, const std::string &xml_string) {
		auto stats = XMLUtils::GetXMLStats(xml_string);

		// Create stats struct
		child_list_t<Value> stats_children;
		stats_children.emplace_back("element_count", Value::BIGINT(stats.element_count));
		stats_children.emplace_back("attribute_count", Value::BIGINT(stats.attribute_count));
		stats_children.emplace_back("max_depth", Value::BIGINT(stats.max_depth));
		stats_children.emplace_back("size_bytes", Value::BIGINT(stats.size_bytes));
		stats_children.emplace_back("namespace_count", Value::BIGINT(stats.namespace_count));

		Value stats_value = Value::STRUCT(stats_children);
		result.SetValue(i, stats_value);
	});
}

void XMLScalarFunctions::XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(xml_vector, result, count, [&](idx_t i, const std::string &xml_string) {
		auto namespaces = XMLUtils::ExtractNamespaces(xml_string);

		// Create MAP(VARCHAR, VARCHAR) with prefix -> uri mappings
		vector<Value> keys;
		vector<Value> values;

		for (const auto &ns : namespaces) {
			// Use empty string for default namespace (no prefix)
			keys.emplace_back(Value(ns.prefix.empty() ? "" : ns.prefix));
			values.emplace_back(Value(ns.uri));
		}

		// Create MAP value
		Value map_value = Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
		result.SetValue(i, map_value);
	});
}

// --- Internal OOM-path regression self-test (xml_oom_selftest) ---
// The libxml2 out-of-memory path cannot be reached from a SQL test, so this exercises it directly:
// an always-failing allocator makes xmlNewParserCtxt return NULL, which must surface as
// XMLValidity::ResourceError (not Malformed), and the resource-failure flag must survive XMLDocRAII
// moves (the read_xml DOM path relies on `gstate.current_doc = XMLDocRAII(...)`). Allocator setup is
// process-global, so it is installed and restored within this single call. On platforms where
// xmlMemSetup is a no-op (e.g. macOS system libxml2) the injected failure has no effect and the
// allocation-failure half is reported as skipped rather than failing.
namespace {
void *OOMFailMalloc(size_t) {
	return nullptr;
}
void *OOMFailRealloc(void *, size_t) {
	return nullptr;
}
char *OOMFailStrdup(const char *) {
	return nullptr;
}
void OOMNoopFree(void *) {
}

std::string RunOOMSelfTest() {
	// Part 1: move operations must carry resource_error (pure, no allocator involved).
	{
		XMLDocRAII a;
		a.resource_error = true;
		XMLDocRAII moved_ctor(std::move(a));
		if (!moved_ctor.HadResourceError()) {
			return "FAIL: move constructor dropped resource_error";
		}
		XMLDocRAII moved_assign;
		moved_assign = std::move(moved_ctor);
		if (!moved_assign.HadResourceError()) {
			return "FAIL: move assignment dropped resource_error";
		}
	}

	// Part 2: an allocation failure must be reported as ResourceError, not Malformed.
	xmlFreeFunc saved_free = nullptr;
	xmlMallocFunc saved_malloc = nullptr;
	xmlReallocFunc saved_realloc = nullptr;
	xmlStrdupFunc saved_strdup = nullptr;
	xmlMemGet(&saved_free, &saved_malloc, &saved_realloc, &saved_strdup);

	if (xmlMemSetup(OOMNoopFree, OOMFailMalloc, OOMFailRealloc, OOMFailStrdup) != 0) {
		return "OK (oom check skipped: custom allocator could not be installed)";
	}
	XMLValidity validity = XMLUtils::CheckXML("<r/>");
	// Restore the real allocators before anything else allocates through libxml2.
	xmlMemSetup(saved_free, saved_malloc, saved_realloc, saved_strdup);

	if (validity == XMLValidity::Valid) {
		// The injected allocator had no effect (no-op on this libxml2 build).
		return "OK (oom check skipped: allocator injection not supported on this platform)";
	}
	if (validity != XMLValidity::ResourceError) {
		return "FAIL: allocation failure reported as Malformed instead of ResourceError";
	}
	return "OK";
}
} // namespace

void XMLScalarFunctions::OOMSelfTestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Run once (it mutates process-global allocator state) and replicate to all output rows.
	std::string status = RunOOMSelfTest();
	for (idx_t i = 0; i < args.size(); i++) {
		result.SetValue(i, Value(status));
	}
}

void XMLScalarFunctions::XMLCommonNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Returns a MAP of well-known namespace prefixes to their URIs
	// This is a constant function that returns the same value for all rows

	auto count = args.size();

	// Define common namespace mappings (prefix -> URI)
	static const vector<pair<string, string>> common_ns = {
	    // XML core namespaces
	    {"xml", "http://www.w3.org/XML/1998/namespace"},
	    {"xmlns", "http://www.w3.org/2000/xmlns/"},

	    // XML Schema
	    {"xsi", "http://www.w3.org/2001/XMLSchema-instance"},
	    {"xsd", "http://www.w3.org/2001/XMLSchema"},
	    {"xs", "http://www.w3.org/2001/XMLSchema"},

	    // Web standards
	    {"xhtml", "http://www.w3.org/1999/xhtml"},
	    {"svg", "http://www.w3.org/2000/svg"},
	    {"xlink", "http://www.w3.org/1999/xlink"},
	    {"mathml", "http://www.w3.org/1998/Math/MathML"},

	    // Syndication
	    {"atom", "http://www.w3.org/2005/Atom"},
	    {"rss", "http://purl.org/rss/1.0/"},

	    // Dublin Core
	    {"dc", "http://purl.org/dc/elements/1.1/"},
	    {"dcterms", "http://purl.org/dc/terms/"},

	    // Semantic Web / RDF
	    {"rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#"},
	    {"rdfs", "http://www.w3.org/2000/01/rdf-schema#"},
	    {"owl", "http://www.w3.org/2002/07/owl#"},
	    {"skos", "http://www.w3.org/2004/02/skos/core#"},
	    {"foaf", "http://xmlns.com/foaf/0.1/"},

	    // Geospatial
	    {"gml", "http://www.opengis.net/gml"},
	    {"gml32", "http://www.opengis.net/gml/3.2"},
	    {"kml", "http://www.opengis.net/kml/2.2"},
	    {"gpx", "http://www.topografix.com/GPX/1/1"},
	    {"georss", "http://www.georss.org/georss"},

	    // SOAP / Web Services
	    {"soap", "http://schemas.xmlsoap.org/soap/envelope/"},
	    {"soap12", "http://www.w3.org/2003/05/soap-envelope"},
	    {"wsdl", "http://schemas.xmlsoap.org/wsdl/"},

	    // Office / Documents
	    {"office", "urn:oasis:names:tc:opendocument:xmlns:office:1.0"},
	    {"odt", "urn:oasis:names:tc:opendocument:xmlns:text:1.0"},
	    {"ods", "urn:oasis:names:tc:opendocument:xmlns:spreadsheet:1.0"},

	    // Other common namespaces
	    {"xslt", "http://www.w3.org/1999/XSL/Transform"},
	    {"exsl", "http://exslt.org/common"}};

	// Build key and value vectors
	vector<Value> keys;
	vector<Value> values;
	for (const auto &ns_pair : common_ns) {
		keys.emplace_back(Value(ns_pair.first));
		values.emplace_back(Value(ns_pair.second));
	}

	// Create the MAP value once
	Value map_value = Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);

	// Set the same value for all rows
	for (idx_t i = 0; i < count; i++) {
		result.SetValue(i, map_value);
	}
}

void XMLScalarFunctions::XMLDetectPrefixesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Detect namespace prefixes used in an XPath expression
	// Returns LIST<VARCHAR> of unique prefixes found

	auto &xpath_vector = args.data[0];
	auto count = args.size();

	// Regex pattern to match namespace prefixes in XPath
	// Matches: prefix:name where prefix is NCName (valid XML name without colon)
	// NCName pattern: [A-Za-z_][A-Za-z0-9._-]*
	// We look for prefix: followed by a valid name character
	// But we need to exclude axis specifiers like "child::", "descendant::", etc.
	static const std::regex prefix_pattern(
	    R"((?:^|[/\[\(@,|])([A-Za-z_][A-Za-z0-9._-]*):(?!:)([A-Za-z_*][A-Za-z0-9._-]*))", std::regex::optimize);

	// XPath axes to exclude (these use :: not single :)
	static const std::set<std::string> xpath_axes = {
	    "ancestor",  "ancestor-or-self",  "attribute", "child",  "descendant", "descendant-or-self",
	    "following", "following-sibling", "namespace", "parent", "preceding",  "preceding-sibling",
	    "self"};

	ExecuteNullSafeString(xpath_vector, result, count, [&](idx_t i, const std::string &xpath) {
		// Find all prefixes
		std::set<std::string> prefixes;
		std::sregex_iterator iter(xpath.begin(), xpath.end(), prefix_pattern);
		std::sregex_iterator end;

		while (iter != end) {
			std::string prefix = (*iter)[1].str();
			// Don't include XPath axis specifiers
			if (xpath_axes.find(prefix) == xpath_axes.end()) {
				prefixes.insert(prefix);
			}
			++iter;
		}

		// Create LIST<VARCHAR> result
		vector<Value> prefix_values;
		for (const auto &prefix : prefixes) {
			prefix_values.emplace_back(Value(prefix));
		}

		Value list_value = Value::LIST(LogicalType::VARCHAR, prefix_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::XMLMockNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Create mock namespace URIs for a list of prefixes
	// Returns MAP<VARCHAR, VARCHAR> with prefix -> urn:mock:prefix mappings

	auto &prefixes_vector = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto prefixes_value = prefixes_vector.GetValue(i);

		vector<Value> keys;
		vector<Value> values;

		if (!prefixes_value.IsNull() && prefixes_value.type().id() == LogicalTypeId::LIST) {
			auto &children = ListValue::GetChildren(prefixes_value);
			for (const auto &child : children) {
				if (!child.IsNull()) {
					std::string prefix = child.ToString();
					keys.emplace_back(Value(prefix));
					values.emplace_back(Value("urn:mock:" + prefix));
				}
			}
		}

		Value map_value = Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
		result.SetValue(i, map_value);
	}
}

void XMLScalarFunctions::XMLFindUndefinedPrefixesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Finds namespace prefixes in the XPath that are not declared in the XML document
	// Returns LIST<VARCHAR> of undefined prefixes

	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();

	UnifiedVectorFormat xml_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(xml_vector, count, xml_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto xml_strings = UnifiedVectorFormat::GetData<string_t>(xml_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto xml_idx = xml_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		if (!xml_data.validity.RowIsValid(xml_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value(result.GetType()));
			continue;
		}

		std::string xml_string = xml_strings[xml_idx].GetString();
		std::string xpath = xpath_strings[xpath_idx].GetString();

		// Get prefixes used in XPath
		auto xpath_prefixes = DetectXPathPrefixes(xpath);

		// Get namespaces declared in document
		XMLDocRAII xml_doc(xml_string);
		auto declared_ns = xml_doc.IsValid() ? xml_doc.GetDeclaredNamespaces() : case_insensitive_map_t<string>();

		// Find undefined prefixes
		vector<Value> undefined_values;
		for (const auto &prefix : xpath_prefixes) {
			// Check if declared in document
			if (declared_ns.find(prefix) == declared_ns.end()) {
				// Also exclude built-in xml prefix
				if (prefix != "xml") {
					undefined_values.emplace_back(Value(prefix));
				}
			}
		}

		Value list_value = Value::LIST(LogicalType::VARCHAR, undefined_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::XMLAddNamespaceDeclarationsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Injects namespace declarations into an XML document's root element
	// Takes XML string and MAP<VARCHAR, VARCHAR> of prefix -> uri mappings
	// Returns modified XML string

	auto &xml_vector = args.data[0];
	auto &ns_map_vector = args.data[1];
	auto count = args.size();

	ExecuteNullSafeString(xml_vector, result, count, [&](idx_t i, const std::string &xml_string) {
		auto ns_map_value = ns_map_vector.GetValue(i);

		// Parse the MAP value into case_insensitive_map
		case_insensitive_map_t<string> namespaces_to_inject;
		if (!ns_map_value.IsNull() && ns_map_value.type().id() == LogicalTypeId::MAP) {
			auto &map_children = MapValue::GetChildren(ns_map_value);
			for (const auto &entry : map_children) {
				auto &struct_children = StructValue::GetChildren(entry);
				if (struct_children.size() == 2 && !struct_children[0].IsNull() && !struct_children[1].IsNull()) {
					std::string prefix = struct_children[0].ToString();
					std::string uri = struct_children[1].ToString();
					namespaces_to_inject[prefix] = uri;
				}
			}
		}

		// Inject the namespaces
		std::string modified_xml = InjectNamespaceDeclarations(xml_string, namespaces_to_inject);
		result.SetValue(i, StringVector::AddString(result, modified_xml));
	});
}

void XMLScalarFunctions::XMLLookupNamespaceFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// Looks up a namespace prefix in the common namespaces table
	// Returns the URI if found, NULL otherwise

	auto &prefix_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(prefix_vector, result, count, [&](idx_t i, const std::string &prefix) {
		std::string uri = GetCommonNamespaceURI(prefix);
		if (uri.empty()) {
			FlatVector::SetNull(result, i, true);
		} else {
			result.SetValue(i, Value(uri));
		}
	});
}

void XMLScalarFunctions::XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		std::string json_string = XMLUtils::XMLToJSON(xml_string);
		return StringVector::AddString(result, json_string);
	});
}

unique_ptr<FunctionData> XMLScalarFunctions::XMLToJSONWithSchemaBind(DUCKDB_SCALAR_BIND_PARAMS) {
	auto &bind_args = DUCKDB_SCALAR_BIND_ARGS;
	auto &bind_ctx = DUCKDB_SCALAR_BIND_CONTEXT;

	if (bind_args.empty()) {
		throw BinderException("xml_to_json requires at least one argument (the XML string)");
	}

	XMLToJSONOptions options; // Start with defaults

	// First argument is the XML string (positional)
	// Note: We don't check for alias here because column references have aliases by default

	// Process named parameters (if any)
	for (idx_t i = 1; i < bind_args.size(); i++) {
		auto &arg = bind_args[i];
		std::string param_name = CompatIdentifierName(arg->GetAlias());

		if (param_name.empty()) {
			throw BinderException(
			    "All arguments after the first must be named parameters (e.g., force_list := ['name'])");
		}

		// Check if the argument is foldable (constant)
		if (arg->HasParameter()) {
			throw ParameterNotResolvedException();
		}
		if (!arg->IsFoldable()) {
			throw BinderException("Parameter '%s' must be a constant value", param_name);
		}

		// Extract the constant value
		Value param_value = ExpressionExecutor::EvaluateScalar(bind_ctx, *arg);

		if (param_name == "force_list") {
			if (param_value.IsNull()) {
				options.force_list.clear(); // NULL means empty list
			} else if (param_value.type().id() != LogicalTypeId::LIST) {
				throw BinderException("force_list parameter must be a list of strings, e.g., ['name', 'item']");
			} else {
				// Check child type only if list is not empty
				auto &list_children = ListValue::GetChildren(param_value);
				if (!list_children.empty() &&
				    ListType::GetChildType(param_value.type()).id() != LogicalTypeId::VARCHAR) {
					throw BinderException("force_list parameter must be a list of strings, e.g., ['name', 'item']");
				}
				options.force_list.clear();
				for (const auto &item : list_children) {
					if (item.IsNull()) {
						throw BinderException("force_list cannot contain NULL values");
					}
					options.force_list.push_back(StringValue::Get(item));
				}
			}
		} else if (param_name == "attr_prefix") {
			if (param_value.IsNull()) {
				options.attr_prefix = "@"; // Default
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("attr_prefix parameter must be a string");
			} else {
				options.attr_prefix = StringValue::Get(param_value);
			}
		} else if (param_name == "text_key") {
			if (param_value.IsNull()) {
				options.text_key = "#text"; // Default
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("text_key parameter must be a string");
			} else {
				options.text_key = StringValue::Get(param_value);
			}
		} else if (param_name == "namespaces") {
			if (param_value.IsNull()) {
				options.namespaces = "strip"; // Default
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("namespaces parameter must be a string");
			} else {
				auto ns_val = StringValue::Get(param_value);
				if (ns_val != "strip" && ns_val != "expand" && ns_val != "keep") {
					throw BinderException("Invalid value for namespaces parameter: Must be one of: 'strip', 'expand', "
					                      "or 'keep', got '%s'",
					                      ns_val);
				}
				options.namespaces = ns_val;
			}
		} else if (param_name == "xmlns_key") {
			if (param_value.IsNull()) {
				options.xmlns_key = ""; // Default (disabled)
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("xmlns_key parameter must be a string");
			} else {
				options.xmlns_key = StringValue::Get(param_value);
			}
		} else if (param_name == "empty_elements") {
			if (param_value.IsNull()) {
				options.empty_elements = "object"; // Default
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("Invalid value for empty_elements: VARCHAR required, got '%s'",
				                      param_value.type().ToString());
			} else {
				auto empty_val = StringValue::Get(param_value);
				if (empty_val != "object" && empty_val != "null" && empty_val != "string") {
					throw BinderException("Invalid value for empty_elements parameter: Must be one of: 'object', "
					                      "'null', or 'string', got '%s'",
					                      empty_val);
				}
				options.empty_elements = empty_val;
			}
		} else {
			throw BinderException("Unknown parameter '%s' for xml_to_json", param_name);
		}
	}

	return make_uniq<XMLToJSONBindData>(options);
}

void XMLScalarFunctions::XMLToJSONWithSchemaFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &func_expr = state.expr.Cast<BoundFunctionExpression>();

	// Get options from bind data, or use defaults if not bound.
	// bind_info became private in newer DuckDB; access it via the BindInfo() accessor there.
	XMLToJSONOptions options;
#ifdef DUCKDB_HAS_NEW_VECTOR_HEADERS
	auto &bind_info = func_expr.BindInfo();
#else
	auto &bind_info = func_expr.bind_info;
#endif
	if (bind_info) {
		auto &bind_data = bind_info->Cast<XMLToJSONBindData>();
		options = bind_data.options;
	}

	auto &xml_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		std::string json_string = XMLUtils::XMLToJSON(xml_string, options);
		return StringVector::AddString(result, json_string);
	});
}

void XMLScalarFunctions::JSONToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &json_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(json_vector, result, args.size(), [&](string_t json_str) {
		std::string json_string = json_str.GetString();
		std::string xml_string = XMLUtils::JSONToXML(json_string);
		return StringVector::AddString(result, xml_string);
	});
}

void XMLScalarFunctions::ValueToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input_vector = args.data[0];
	auto &input_type = input_vector.GetType();

	// Type debugging (can be removed in production)
	// printf("DEBUG to_xml: input_type=%s, id=%d, has_alias=%s, alias=%s\n",
	//	input_type.ToString().c_str(),
	//	(int)input_type.id(),
	//	input_type.HasAlias() ? "true" : "false",
	//	input_type.HasAlias() ? input_type.GetAlias().c_str() : "none");

	// Get node name (default "xml" if not provided)
	std::string default_node_name = "xml";
	if (args.ColumnCount() == 2) {
		// Node name provided as second argument - for now, assume it's constant
		// TODO: Handle variable node names per row
		auto &node_name_vector = args.data[1];
		if (node_name_vector.GetVectorType() == VectorType::CONSTANT_VECTOR) {
			auto node_name_data = ConstantVector::GetData<string_t>(node_name_vector);
			if (!ConstantVector::IsNull(node_name_vector)) {
				default_node_name = node_name_data->GetString();
			}
		}
	}
	// The node name becomes an element name; reject any that would inject markup into the
	// trusted xml-typed output (e.g. to_xml(v, 'a><evil')).
	XMLUtils::ValidateXMLElementName(default_node_name);

	// Apply our type hierarchy
	if (XMLTypes::IsXMLFragmentType(input_type)) {
		// XMLFragment → Insert verbatim
		UnaryExecutor::Execute<string_t, string_t>(input_vector, result, args.size(), [&](string_t input) {
			return StringVector::AddString(result, input.GetString());
		});
	} else if (XMLTypes::IsXMLType(input_type)) {
		// XML → Insert verbatim
		UnaryExecutor::Execute<string_t, string_t>(input_vector, result, args.size(), [&](string_t input) {
			return StringVector::AddString(result, input.GetString());
		});
	} else if (input_type.id() == LogicalTypeId::LIST) {
		// LIST → Recursive conversion
		XMLUtils::ConvertListToXML(input_vector, result, args.size(), default_node_name);
	} else if (input_type.id() == LogicalTypeId::STRUCT) {
		// STRUCT → Recursive conversion
		XMLUtils::ConvertStructToXML(input_vector, result, args.size(), default_node_name);
	} else {
		// Check if this is an explicit JSON type (has JSON alias)
		bool is_json_type = false;

		try {
			// Only check for explicit JSON type (has JSON alias)
			is_json_type =
			    (input_type.id() == LogicalTypeId::VARCHAR && input_type.HasAlias() && input_type.GetAlias() == "JSON");
		} catch (...) {
			// Error in detection, treat as non-JSON
			is_json_type = false;
		}

		if (is_json_type) {
			// JSON → Structural conversion (same as JSON::XML casting)
			UnaryExecutor::Execute<string_t, string_t>(input_vector, result, args.size(), [&](string_t json_input) {
				std::string json_str = json_input.GetString();
				std::string xml_result = XMLUtils::JSONToXML(json_str);
				return StringVector::AddString(result, xml_result);
			});
		} else {
			// STRING/Other → Convert to string representation, then to XML
			for (idx_t i = 0; i < args.size(); i++) {
				Value input_value = input_vector.GetValue(i);
				std::string input_str;

				if (input_value.IsNull()) {
					// NULL in -> NULL out
					result.SetValue(i, Value(result.GetType()));
					continue;
				} else if (input_type.id() == LogicalTypeId::VARCHAR) {
					input_str = input_value.GetValue<string>();
				} else {
					// Convert any other type to string representation
					input_str = input_value.ToString();
				}

				// Check if input is already valid XML (only for string types)
				if (input_type.id() == LogicalTypeId::VARCHAR && XMLUtils::IsValidXML(input_str)) {
					result.SetValue(i, Value(input_str));
				} else {
					// Convert scalar value to XML using libxml2
					std::string xml_result = XMLUtils::ScalarToXML(input_str, default_node_name);
					result.SetValue(i, Value(xml_result));
				}
			}
		}
	}
}

void XMLScalarFunctions::Register(ExtensionLoader &loader) {
	// Helper: add a base (2-argument) extract overload that also accepts an optional trailing
	// `namespaces` argument, supplied positionally or as `namespaces := <map/mode>`. Marking the
	// overload as varargs makes DuckDB's binder accept the named argument as a named vararg; the
	// execute function then delegates to its namespace-aware sibling when a third column is present.
	// Required for forward-compat with DuckDB main, which (unlike <=v1.5.3) no longer silently
	// ignores argument labels on scalar functions. (GitHub Issue #78 follow-up / DuckDB main drift.)
	auto add_ns_aware = [](ScalarFunctionSet &set, ScalarFunction fn) {
		SetScalarFunctionVarArgs(fn, LogicalType::ANY);
		set.AddFunction(std::move(fn));
	};

	// Register xml function (same as to_xml for now) - using VARCHAR for now, will enhance type system later
	auto xml_function = ScalarFunction("xml", {LogicalType::VARCHAR}, LogicalType::VARCHAR, ValueToXMLFunction);
	loader.RegisterFunction(xml_function);

	// Register to_xml function (single argument) - ANY type variant (unified path)
	auto to_xml_any_function = ScalarFunction("to_xml", {LogicalType::ANY}, XMLTypes::XMLType(), ValueToXMLFunction);
	loader.RegisterFunction(to_xml_any_function);

	// Register to_xml function (two arguments: value, node_name) - ANY type variant (unified path)
	auto to_xml_any_with_name_function =
	    ScalarFunction("to_xml", {LogicalType::ANY, LogicalType::VARCHAR}, XMLTypes::XMLType(), ValueToXMLFunction);
	loader.RegisterFunction(to_xml_any_with_name_function);

	// Register xml_libxml2_version function
	auto xml_libxml2_version_function = ScalarFunction(
	    "xml_libxml2_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &name_vector = args.data[0];
		    UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
			    return StringVector::AddString(result,
			                                   "Xml " + name.GetString() + ", my linked libxml2 version is 2.13.8");
		    });
	    });
	loader.RegisterFunction(xml_libxml2_version_function);

	// Register xml_valid function - both XML and VARCHAR overloads
	auto xml_valid_function =
	    ScalarFunction("xml_valid", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLValidFunction);
	loader.RegisterFunction(xml_valid_function);
	auto xml_valid_varchar_function =
	    ScalarFunction("xml_valid", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidFunction);
	loader.RegisterFunction(xml_valid_varchar_function);

	// Register xml_well_formed function - both XML and VARCHAR overloads
	auto xml_well_formed_function =
	    ScalarFunction("xml_well_formed", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	loader.RegisterFunction(xml_well_formed_function);
	auto xml_well_formed_varchar_function =
	    ScalarFunction("xml_well_formed", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	loader.RegisterFunction(xml_well_formed_varchar_function);

	// Register xml_extract_text function - returns LIST(VARCHAR) (PostgreSQL-compatible)
	// Use list[1] or list_extract(list, 1) to get single value
	ScalarFunctionSet xml_extract_text_functions("xml_extract_text");

	// XML + VARCHAR -> LIST(VARCHAR)
	add_ns_aware(xml_extract_text_functions, ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR},
	                                                        LogicalType::LIST(LogicalType::VARCHAR),
	                                                        XMLExtractTextListFunction));
	// XML + STRING_LITERAL -> LIST(VARCHAR)
	// (no varargs on STRING_LITERAL overloads: varargs + a literal parameter makes DuckDB resolve a
	//  literal return type and trip an internal error. The named-arg case routes through the VARCHAR
	//  overload below, with the literal xpath implicitly cast to VARCHAR.)
	xml_extract_text_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLExtractTextListFunction));
	// XMLFragment + VARCHAR -> LIST(VARCHAR)
	add_ns_aware(xml_extract_text_functions, ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR},
	                                                        LogicalType::LIST(LogicalType::VARCHAR),
	                                                        XMLExtractTextListFunction));
	// XMLFragment + STRING_LITERAL -> LIST(VARCHAR)
	xml_extract_text_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLExtractTextListFunction));
	// VARCHAR + VARCHAR -> LIST(VARCHAR) (compatibility)
	add_ns_aware(xml_extract_text_functions, ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                                        LogicalType::LIST(LogicalType::VARCHAR),
	                                                        XMLExtractTextListFunction));
	// VARCHAR + STRING_LITERAL -> LIST(VARCHAR) (compatibility)
	xml_extract_text_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLExtractTextListFunction));

	// 3-argument variants with namespaces MAP
	auto ns_map_type = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
	// XML + VARCHAR + MAP -> LIST(VARCHAR)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, ns_map_type},
	                                                      LogicalType::LIST(LogicalType::VARCHAR),
	                                                      XMLExtractTextListWithNamespacesFunction));
	// VARCHAR + VARCHAR + MAP -> LIST(VARCHAR)
	xml_extract_text_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, ns_map_type},
	                                                      LogicalType::LIST(LogicalType::VARCHAR),
	                                                      XMLExtractTextListWithNamespacesFunction));

	// 3-argument variants with namespace mode VARCHAR ('auto', 'strict', 'ignore')
	// XML + VARCHAR + VARCHAR (mode) -> LIST(VARCHAR)
	xml_extract_text_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLExtractTextListWithNamespacesFunction));
	// VARCHAR + VARCHAR + VARCHAR (mode) -> LIST(VARCHAR)
	xml_extract_text_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLExtractTextListWithNamespacesFunction));

	loader.RegisterFunction(xml_extract_text_functions);

	auto projected_record_type = LogicalType::STRUCT(
	    {{"group_index", LogicalType::BIGINT}, {"values", LogicalType::LIST(LogicalType::VARCHAR)}});
	auto xml_project_records_function = ScalarFunction(
	    "xml_project_records",
	    {LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::BIGINT),
	     LogicalType::LIST(LogicalType::BIGINT), LogicalType::LIST(LogicalType::VARCHAR)},
	    LogicalType::LIST(projected_record_type), XMLProjectRecordsFunction, XMLProjectRecordsBind);
	PreventStructConstantFolding(xml_project_records_function);
	loader.RegisterFunction(xml_project_records_function);
	auto xml_project_records_dom_function = ScalarFunction(
	    "xml_project_records_dom",
	    {LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::BIGINT),
	     LogicalType::LIST(LogicalType::BIGINT), LogicalType::LIST(LogicalType::VARCHAR)},
	    LogicalType::LIST(projected_record_type), XMLProjectRecordsDOMFunction, XMLProjectRecordsBind);
	PreventStructConstantFolding(xml_project_records_dom_function);
	loader.RegisterFunction(xml_project_records_dom_function);
	auto xml_project_records_sax_function = ScalarFunction(
	    "xml_project_records_sax",
	    {LogicalType::VARCHAR, LogicalType::LIST(LogicalType::VARCHAR), LogicalType::LIST(LogicalType::BIGINT),
	     LogicalType::LIST(LogicalType::BIGINT), LogicalType::LIST(LogicalType::VARCHAR)},
	    LogicalType::LIST(projected_record_type), XMLProjectRecordsSAXFunction, XMLProjectRecordsBind);
	PreventStructConstantFolding(xml_project_records_sax_function);
	loader.RegisterFunction(xml_project_records_sax_function);

	// Register xml_extract_all_text function - both XML and VARCHAR overloads
	auto xml_extract_all_text_function =
	    ScalarFunction("xml_extract_all_text", {XMLTypes::XMLType()}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	loader.RegisterFunction(xml_extract_all_text_function);
	auto xml_extract_all_text_varchar_function =
	    ScalarFunction("xml_extract_all_text", {LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	loader.RegisterFunction(xml_extract_all_text_varchar_function);

	// Register xml_extract_elements function - returns LIST(XMLFragment) (PostgreSQL-compatible)
	// Use list[1] or list_extract(list, 1) to get single value
	ScalarFunctionSet xml_extract_elements_functions("xml_extract_elements");

	// XML + VARCHAR -> LIST(XMLFragment)
	add_ns_aware(xml_extract_elements_functions, ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR},
	                                                            LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                            XMLExtractElementsListFunction));
	// XML + STRING_LITERAL -> LIST(XMLFragment)
	// (STRING_LITERAL overloads stay varargs-free; see note on xml_extract_text above.)
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListFunction));
	// HTML + VARCHAR -> LIST(XMLFragment)
	add_ns_aware(xml_extract_elements_functions, ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR},
	                                                            LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                            XMLExtractElementsListFunction));
	// HTML + STRING_LITERAL -> LIST(XMLFragment)
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListFunction));
	// XMLFragment + VARCHAR -> LIST(XMLFragment) (for nested extraction)
	add_ns_aware(xml_extract_elements_functions, ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR},
	                                                            LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                            XMLExtractElementsListFunction));
	// XMLFragment + STRING_LITERAL -> LIST(XMLFragment)
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListFunction));
	// VARCHAR + VARCHAR -> LIST(XMLFragment) (compatibility)
	add_ns_aware(xml_extract_elements_functions, ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                                            LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                            XMLExtractElementsListFunction));
	// VARCHAR + STRING_LITERAL -> LIST(XMLFragment) (compatibility)
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListFunction));

	// 3-argument variants with namespaces MAP
	// XML + VARCHAR + MAP -> LIST(XMLFragment)
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, ns_map_type},
	                                                          LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                          XMLExtractElementsListWithNamespacesFunction));
	// VARCHAR + VARCHAR + MAP -> LIST(XMLFragment)
	xml_extract_elements_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, ns_map_type},
	                                                          LogicalType::LIST(XMLTypes::XMLFragmentType()),
	                                                          XMLExtractElementsListWithNamespacesFunction));

	// 3-argument variants with namespace mode VARCHAR ('auto', 'strict', 'ignore')
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListWithNamespacesFunction));
	xml_extract_elements_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(XMLTypes::XMLFragmentType()), XMLExtractElementsListWithNamespacesFunction));

	loader.RegisterFunction(xml_extract_elements_functions);

	// Register xml_extract_elements_string function as a function set
	ScalarFunctionSet xml_extract_elements_string_functions("xml_extract_elements_string");
	add_ns_aware(xml_extract_elements_string_functions,
	             ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                            XMLExtractElementsStringFunction));
	add_ns_aware(xml_extract_elements_string_functions,
	             ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                            XMLExtractElementsStringFunction));
	// 3-argument variants with namespaces MAP
	xml_extract_elements_string_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, ns_map_type}, LogicalType::VARCHAR,
	                   XMLExtractElementsStringWithNamespacesFunction));
	xml_extract_elements_string_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, ns_map_type}, LogicalType::VARCHAR,
	                   XMLExtractElementsStringWithNamespacesFunction));
	// 3-argument variants with namespace mode VARCHAR
	xml_extract_elements_string_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                   XMLExtractElementsStringWithNamespacesFunction));
	xml_extract_elements_string_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                   XMLExtractElementsStringWithNamespacesFunction));
	loader.RegisterFunction(xml_extract_elements_string_functions);

	// Register xml_wrap_fragment function (returns XML)
	auto xml_wrap_fragment_function = ScalarFunction("xml_wrap_fragment", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                                 XMLTypes::XMLType(), XMLWrapFragmentFunction);
	loader.RegisterFunction(xml_wrap_fragment_function);

	// Register xml_extract_attributes function (returns LIST<STRUCT>)
	auto attr_struct_type = LogicalType::STRUCT(
	    {make_pair("element_name", LogicalType::VARCHAR), make_pair("element_path", LogicalType::VARCHAR),
	     make_pair("attribute_name", LogicalType::VARCHAR), make_pair("attribute_value", LogicalType::VARCHAR),
	     make_pair("line_number", LogicalType::BIGINT)});
	// Register xml_extract_attributes function as a function set
	ScalarFunctionSet xml_extract_attributes_functions("xml_extract_attributes");
	add_ns_aware(xml_extract_attributes_functions, ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR},
	                                                              LogicalType::LIST(attr_struct_type),
	                                                              XMLExtractAttributesFunction));
	add_ns_aware(xml_extract_attributes_functions, ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR},
	                                                              LogicalType::LIST(attr_struct_type),
	                                                              XMLExtractAttributesFunction));
	add_ns_aware(xml_extract_attributes_functions, ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                                              LogicalType::LIST(attr_struct_type),
	                                                              XMLExtractAttributesFunction));
	// Add 3-argument variants with namespace map
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, ns_map_type}, LogicalType::LIST(attr_struct_type),
	                   XMLExtractAttributesWithNamespacesFunction));
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR, ns_map_type}, LogicalType::LIST(attr_struct_type),
	                   XMLExtractAttributesWithNamespacesFunction));
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, ns_map_type}, LogicalType::LIST(attr_struct_type),
	                   XMLExtractAttributesWithNamespacesFunction));
	// Add 3-argument variants with namespace mode VARCHAR
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(attr_struct_type), XMLExtractAttributesWithNamespacesFunction));
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(attr_struct_type), XMLExtractAttributesWithNamespacesFunction));
	xml_extract_attributes_functions.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(attr_struct_type), XMLExtractAttributesWithNamespacesFunction));
	PreventStructConstantFolding(xml_extract_attributes_functions);
	loader.RegisterFunction(xml_extract_attributes_functions);

	// Register xml_pretty_print function
	auto xml_pretty_print_function =
	    ScalarFunction("xml_pretty_print", {LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLPrettyPrintFunction);
	loader.RegisterFunction(xml_pretty_print_function);

	// Register xml_minify function
	auto xml_minify_function =
	    ScalarFunction("xml_minify", {LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLMinifyFunction);
	loader.RegisterFunction(xml_minify_function);

	// Register xml_validate_schema function
	auto xml_validate_schema_function =
	    ScalarFunction("xml_validate_schema", {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::BOOLEAN,
	                   XMLValidateSchemaFunction);
	loader.RegisterFunction(xml_validate_schema_function);

	// Register xml_extract_comments function (returns LIST<STRUCT>)
	auto comment_struct_type = LogicalType::STRUCT(
	    {make_pair("content", LogicalType::VARCHAR), make_pair("line_number", LogicalType::BIGINT)});
	auto xml_extract_comments_function =
	    ScalarFunction("xml_extract_comments", {XMLTypes::XMLType()}, LogicalType::LIST(comment_struct_type),
	                   XMLExtractCommentsFunction);
	PreventStructConstantFolding(xml_extract_comments_function);
	loader.RegisterFunction(xml_extract_comments_function);

	// Register xml_extract_cdata function (returns LIST<STRUCT>)
	auto xml_extract_cdata_function = ScalarFunction("xml_extract_cdata", {XMLTypes::XMLType()},
	                                                 LogicalType::LIST(comment_struct_type), XMLExtractCDataFunction);
	PreventStructConstantFolding(xml_extract_cdata_function);
	loader.RegisterFunction(xml_extract_cdata_function);

	// Register xml_stats function (returns STRUCT)
	auto stats_struct_type = LogicalType::STRUCT(
	    {make_pair("element_count", LogicalType::BIGINT), make_pair("attribute_count", LogicalType::BIGINT),
	     make_pair("max_depth", LogicalType::BIGINT), make_pair("size_bytes", LogicalType::BIGINT),
	     make_pair("namespace_count", LogicalType::BIGINT)});
	auto xml_stats_function = ScalarFunction("xml_stats", {LogicalType::VARCHAR}, stats_struct_type, XMLStatsFunction);
	PreventStructConstantFolding(xml_stats_function);
	loader.RegisterFunction(xml_stats_function);

	// Register xml_namespaces function (returns MAP<VARCHAR, VARCHAR> with prefix -> uri mappings)
	auto xml_namespaces_function =
	    ScalarFunction("xml_namespaces", {LogicalType::VARCHAR},
	                   LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), XMLNamespacesFunction);
	PreventStructConstantFolding(xml_namespaces_function);
	loader.RegisterFunction(xml_namespaces_function);

	// Register xml_common_namespaces function (returns MAP<VARCHAR, VARCHAR> of well-known namespace prefixes)
	// This is a constant function that takes no arguments
	auto xml_common_namespaces_function =
	    ScalarFunction("xml_common_namespaces", {}, LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR),
	                   XMLCommonNamespacesFunction);
	PreventStructConstantFolding(xml_common_namespaces_function);
	loader.RegisterFunction(xml_common_namespaces_function);

	// Internal regression self-test for the libxml2 out-of-memory path (see OOMSelfTestFunction).
	// Returns 'OK' (or 'OK (oom check skipped...)' where the allocator can't be injected); any other
	// value indicates a regression. Result is deterministic, so default stability is fine.
	loader.RegisterFunction(
	    ScalarFunction("xml_oom_selftest", {}, LogicalType::VARCHAR, OOMSelfTestFunction));

	// Register xml_detect_prefixes function (returns LIST<VARCHAR> of namespace prefixes in XPath expression)
	auto xml_detect_prefixes_function =
	    ScalarFunction("xml_detect_prefixes", {LogicalType::VARCHAR}, LogicalType::LIST(LogicalType::VARCHAR),
	                   XMLDetectPrefixesFunction);
	loader.RegisterFunction(xml_detect_prefixes_function);

	// Register xml_mock_namespaces function (returns MAP<VARCHAR, VARCHAR> with mock URIs for prefixes)
	auto xml_mock_namespaces_function =
	    ScalarFunction("xml_mock_namespaces", {LogicalType::LIST(LogicalType::VARCHAR)},
	                   LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), XMLMockNamespacesFunction);
	PreventStructConstantFolding(xml_mock_namespaces_function);
	loader.RegisterFunction(xml_mock_namespaces_function);

	// Register xml_find_undefined_prefixes function
	// Finds namespace prefixes used in an XPath expression that are not declared in the XML document
	auto xml_find_undefined_prefixes_function =
	    ScalarFunction("xml_find_undefined_prefixes", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::LIST(LogicalType::VARCHAR), XMLFindUndefinedPrefixesFunction);
	loader.RegisterFunction(xml_find_undefined_prefixes_function);

	// Register xml_add_namespace_declarations function
	// Injects xmlns declarations into an XML document's root element
	auto xml_add_namespace_declarations_function =
	    ScalarFunction("xml_add_namespace_declarations",
	                   {LogicalType::VARCHAR, LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)},
	                   LogicalType::VARCHAR, XMLAddNamespaceDeclarationsFunction);
	loader.RegisterFunction(xml_add_namespace_declarations_function);

	// Register xml_lookup_namespace function
	// Looks up a namespace prefix in the common namespaces table
	auto xml_lookup_namespace_function = ScalarFunction("xml_lookup_namespace", {LogicalType::VARCHAR},
	                                                    LogicalType::VARCHAR, XMLLookupNamespaceFunction);
	SetScalarFunctionNullHandling(xml_lookup_namespace_function, FunctionNullHandling::SPECIAL_HANDLING);
	loader.RegisterFunction(xml_lookup_namespace_function);

	// Register xml_to_json function with optional named parameters
	ScalarFunction xml_to_json_function("xml_to_json", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                    XMLToJSONWithSchemaFunction, XMLToJSONWithSchemaBind);
	SetScalarFunctionVarArgs(xml_to_json_function, LogicalType::ANY);
	SetScalarFunctionNullHandling(xml_to_json_function, FunctionNullHandling::SPECIAL_HANDLING);
	loader.RegisterFunction(xml_to_json_function);

	// Register json_to_xml function
	auto json_to_xml_function =
	    ScalarFunction("json_to_xml", {LogicalType::VARCHAR}, LogicalType::VARCHAR, JSONToXMLFunction);
	loader.RegisterFunction(json_to_xml_function);

	// Register HTML extraction functions following markdown extension patterns

	// Define return types for HTML functions
	auto html_link_struct_type = LogicalType::STRUCT({{"text", LogicalType(LogicalTypeId::VARCHAR)},
	                                                  {"href", LogicalType(LogicalTypeId::VARCHAR)},
	                                                  {"title", LogicalType(LogicalTypeId::VARCHAR)},
	                                                  {"line_number", LogicalType(LogicalTypeId::BIGINT)}});

	auto html_image_struct_type = LogicalType::STRUCT({{"alt", LogicalType(LogicalTypeId::VARCHAR)},
	                                                   {"src", LogicalType(LogicalTypeId::VARCHAR)},
	                                                   {"title", LogicalType(LogicalTypeId::VARCHAR)},
	                                                   {"width", LogicalType(LogicalTypeId::BIGINT)},
	                                                   {"height", LogicalType(LogicalTypeId::BIGINT)},
	                                                   {"line_number", LogicalType(LogicalTypeId::BIGINT)}});

	auto html_table_row_struct_type = LogicalType::STRUCT({{"table_index", LogicalType(LogicalTypeId::BIGINT)},
	                                                       {"row_type", LogicalType(LogicalTypeId::VARCHAR)},
	                                                       {"row_index", LogicalType(LogicalTypeId::BIGINT)},
	                                                       {"column_index", LogicalType(LogicalTypeId::BIGINT)},
	                                                       {"cell_value", LogicalType(LogicalTypeId::VARCHAR)},
	                                                       {"line_number", LogicalType(LogicalTypeId::BIGINT)},
	                                                       {"num_columns", LogicalType(LogicalTypeId::BIGINT)},
	                                                       {"num_rows", LogicalType(LogicalTypeId::BIGINT)}});

	auto html_table_json_struct_type = LogicalType::STRUCT({
	    {"table_index", LogicalType(LogicalTypeId::BIGINT)},
	    {"line_number", LogicalType(LogicalTypeId::BIGINT)},
	    {"num_columns", LogicalType(LogicalTypeId::BIGINT)},
	    {"num_rows", LogicalType(LogicalTypeId::BIGINT)},
	    {"headers", LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))},
	    {"table_data", LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)))},
	    {"table_json", LogicalType::STRUCT({})},    // Complex nested struct
	    {"json_structure", LogicalType::STRUCT({})} // Complex nested struct
	});

	// Register html_extract_text function with XPath support
	// With XPath: returns LIST(VARCHAR) (PostgreSQL-compatible) - use list[1] to get single value
	// Without XPath: returns VARCHAR (all text content concatenated)
	ScalarFunctionSet html_extract_text_functions("html_extract_text");

	// HTML only (no XPath) -> VARCHAR (all text concatenated, unchanged behavior)
	html_extract_text_functions.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType()}, LogicalType::VARCHAR, HTMLExtractTextFunction));

	// HTML + VARCHAR XPath -> LIST(VARCHAR)
	html_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR},
	                                                       LogicalType::LIST(LogicalType::VARCHAR),
	                                                       HTMLExtractTextListFunction));
	// HTML + STRING_LITERAL XPath -> LIST(VARCHAR)
	html_extract_text_functions.AddFunction(
	    ScalarFunction({XMLTypes::HTMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)},
	                   LogicalType::LIST(LogicalType::VARCHAR), HTMLExtractTextListFunction));
	// NOTE: Namespace parameter overloads intentionally omitted for html_extract_text.
	// HTML5 parsing (htmlReadMemory) doesn't support XML namespace declarations -
	// prefixed elements like "svg:circle" are treated as literal names with colons.
	// Users should use name()="prefix:element" XPath predicates for HTML content.

	loader.RegisterFunction(html_extract_text_functions);

	// Register html_extract_links function
	auto html_extract_links_function =
	    ScalarFunction("html_extract_links", {XMLTypes::HTMLType()}, LogicalType::LIST(html_link_struct_type),
	                   HTMLExtractLinksFunction);
	PreventStructConstantFolding(html_extract_links_function);
	loader.RegisterFunction(html_extract_links_function);

	// Register html_extract_images function
	auto html_extract_images_function =
	    ScalarFunction("html_extract_images", {XMLTypes::HTMLType()}, LogicalType::LIST(html_image_struct_type),
	                   HTMLExtractImagesFunction);
	PreventStructConstantFolding(html_extract_images_function);
	loader.RegisterFunction(html_extract_images_function);

	// Register html_extract_table_rows function
	auto html_extract_table_rows_function =
	    ScalarFunction("html_extract_table_rows", {XMLTypes::HTMLType()}, LogicalType::LIST(html_table_row_struct_type),
	                   HTMLExtractTableRowsFunction);
	PreventStructConstantFolding(html_extract_table_rows_function);
	loader.RegisterFunction(html_extract_table_rows_function);

	// Register html_extract_tables_json function
	auto html_extract_tables_json_function =
	    ScalarFunction("html_extract_tables_json", {XMLTypes::HTMLType()},
	                   LogicalType::LIST(html_table_json_struct_type), HTMLExtractTablesJSONFunction);
	PreventStructConstantFolding(html_extract_tables_json_function);
	loader.RegisterFunction(html_extract_tables_json_function);

	// Register parse_html scalar function for parsing HTML content directly
	auto parse_html_function =
	    ScalarFunction("parse_html", {LogicalType::VARCHAR}, XMLTypes::HTMLType(), ReadHTMLFunction);
	loader.RegisterFunction(parse_html_function);

	// Register html_unescape function
	auto html_unescape_function =
	    ScalarFunction("html_unescape", {LogicalType::VARCHAR}, LogicalType::VARCHAR, HTMLUnescapeFunction);
	loader.RegisterFunction(html_unescape_function);

	// Register html_escape function
	auto html_escape_function =
	    ScalarFunction("html_escape", {LogicalType::VARCHAR}, LogicalType::VARCHAR, HTMLEscapeFunction);
	loader.RegisterFunction(html_escape_function);
}

// HTML-specific extraction function implementations
void XMLScalarFunctions::HTMLExtractTextFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(html_vector, result, args.size(), [&](string_t html_str) {
		std::string html_string = html_str.GetString();
		std::string extracted_text = XMLUtils::ExtractHTMLText(html_string);
		return StringVector::AddString(result, extracted_text);
	});
}

void XMLScalarFunctions::HTMLExtractTextWithXPathFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto &xpath_vector = args.data[1];

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    html_vector, xpath_vector, result, args.size(), [&](string_t html_str, string_t xpath_str) {
		    std::string html_string = html_str.GetString();
		    std::string xpath_string = xpath_str.GetString();
		    std::string extracted_text = XMLUtils::ExtractHTMLTextByXPath(html_string, xpath_string);
		    return StringVector::AddString(result, extracted_text);
	    });
}

// Returns LIST(VARCHAR) of all matching HTML text content (PostgreSQL-compatible)
void XMLScalarFunctions::HTMLExtractTextListFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();

	// Use UnifiedVectorFormat to handle different vector types (FLAT, DICTIONARY, CONSTANT, etc.)
	UnifiedVectorFormat html_data;
	UnifiedVectorFormat xpath_data;
	CompatToUnifiedFormat(html_vector, count, html_data);
	CompatToUnifiedFormat(xpath_vector, count, xpath_data);

	auto html_strings = UnifiedVectorFormat::GetData<string_t>(html_data);
	auto xpath_strings = UnifiedVectorFormat::GetData<string_t>(xpath_data);

	for (idx_t i = 0; i < count; i++) {
		auto html_idx = html_data.sel->get_index(i);
		auto xpath_idx = xpath_data.sel->get_index(i);

		// Handle NULL values
		if (!html_data.validity.RowIsValid(html_idx) || !xpath_data.validity.RowIsValid(xpath_idx)) {
			result.SetValue(i, Value::LIST(LogicalType::VARCHAR, vector<Value>()));
			continue;
		}

		std::string html_string = html_strings[html_idx].GetString();
		std::string xpath_string = xpath_strings[xpath_idx].GetString();

		// Return LIST of all matches
		auto texts = XMLUtils::ExtractHTMLAllTextByXPath(html_string, xpath_string);
		vector<Value> text_values;
		for (const auto &text : texts) {
			text_values.push_back(Value(text));
		}
		result.SetValue(i, Value::LIST(LogicalType::VARCHAR, text_values));
	}
}

// NOTE: HTMLExtractTextListWithNamespacesFunction intentionally omitted.
// HTML5 parsing doesn't support XML namespace declarations - prefixed elements
// like "svg:circle" are treated as literal names with colons.
// Users should use name()="prefix:element" XPath predicates for HTML content.

void XMLScalarFunctions::HTMLExtractLinksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(html_vector, result, count, [&](idx_t i, const std::string &html_string) {
		auto links = XMLUtils::ExtractHTMLLinks(html_string);

		vector<Value> link_values;
		for (const auto &link : links) {
			child_list_t<Value> link_children;
			link_children.emplace_back("text", Value(link.text));
			link_children.emplace_back("href", Value(link.url));
			link_children.emplace_back("title", link.title.empty() ? Value() : Value(link.title));
			link_children.emplace_back("line_number", Value::BIGINT(link.line_number));

			link_values.emplace_back(Value::STRUCT(link_children));
		}

		auto link_struct_type = LogicalType::STRUCT(
		    {make_pair("text", LogicalType::VARCHAR), make_pair("href", LogicalType::VARCHAR),
		     make_pair("title", LogicalType::VARCHAR), make_pair("line_number", LogicalType::BIGINT)});

		Value list_value = Value::LIST(link_struct_type, link_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::HTMLExtractImagesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(html_vector, result, count, [&](idx_t i, const std::string &html_string) {
		auto images = XMLUtils::ExtractHTMLImages(html_string);

		vector<Value> image_values;
		for (const auto &image : images) {
			child_list_t<Value> image_children;
			image_children.emplace_back("alt", Value(image.alt_text));
			image_children.emplace_back("src", Value(image.src));
			image_children.emplace_back("title", image.title.empty() ? Value() : Value(image.title));
			image_children.emplace_back("width", Value::BIGINT(image.width));
			image_children.emplace_back("height", Value::BIGINT(image.height));
			image_children.emplace_back("line_number", Value::BIGINT(image.line_number));

			image_values.emplace_back(Value::STRUCT(image_children));
		}

		auto image_struct_type = LogicalType::STRUCT(
		    {make_pair("alt", LogicalType::VARCHAR), make_pair("src", LogicalType::VARCHAR),
		     make_pair("title", LogicalType::VARCHAR), make_pair("width", LogicalType::BIGINT),
		     make_pair("height", LogicalType::BIGINT), make_pair("line_number", LogicalType::BIGINT)});

		Value list_value = Value::LIST(image_struct_type, image_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::HTMLExtractTableRowsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(html_vector, result, count, [&](idx_t i, const std::string &html_string) {
		auto tables = XMLUtils::ExtractHTMLTables(html_string);

		vector<Value> row_values;

		// Process each table
		for (size_t table_idx = 0; table_idx < tables.size(); table_idx++) {
			const auto &table = tables[table_idx];

			// Output header cells
			for (size_t col_idx = 0; col_idx < table.headers.size(); col_idx++) {
				child_list_t<Value> row_children;
				row_children.emplace_back("table_index", Value::BIGINT(static_cast<int64_t>(table_idx)));
				row_children.emplace_back("row_type", Value("header"));
				row_children.emplace_back("row_index", Value::BIGINT(0));
				row_children.emplace_back("column_index", Value::BIGINT(static_cast<int64_t>(col_idx)));
				row_children.emplace_back("cell_value", Value(table.headers[col_idx]));
				row_children.emplace_back("line_number", Value::BIGINT(table.line_number));
				row_children.emplace_back("num_columns", Value::BIGINT(table.num_columns));
				row_children.emplace_back("num_rows", Value::BIGINT(table.num_rows));
				row_values.emplace_back(Value::STRUCT(row_children));
			}

			// Output data rows
			for (size_t row_idx = 0; row_idx < table.rows.size(); row_idx++) {
				const auto &row = table.rows[row_idx];
				for (size_t col_idx = 0; col_idx < row.size(); col_idx++) {
					child_list_t<Value> row_children;
					row_children.emplace_back("table_index", Value::BIGINT(static_cast<int64_t>(table_idx)));
					row_children.emplace_back("row_type", Value("data"));
					row_children.emplace_back("row_index", Value::BIGINT(static_cast<int64_t>(row_idx + 1)));
					row_children.emplace_back("column_index", Value::BIGINT(static_cast<int64_t>(col_idx)));
					row_children.emplace_back("cell_value", Value(row[col_idx]));
					row_children.emplace_back("line_number", Value::BIGINT(table.line_number));
					row_children.emplace_back("num_columns", Value::BIGINT(table.num_columns));
					row_children.emplace_back("num_rows", Value::BIGINT(table.num_rows));
					row_values.emplace_back(Value::STRUCT(row_children));
				}
			}
		}

		auto table_row_struct_type = LogicalType::STRUCT(
		    {make_pair("table_index", LogicalType::BIGINT), make_pair("row_type", LogicalType::VARCHAR),
		     make_pair("row_index", LogicalType::BIGINT), make_pair("column_index", LogicalType::BIGINT),
		     make_pair("cell_value", LogicalType::VARCHAR), make_pair("line_number", LogicalType::BIGINT),
		     make_pair("num_columns", LogicalType::BIGINT), make_pair("num_rows", LogicalType::BIGINT)});

		Value list_value = Value::LIST(table_row_struct_type, row_values);
		result.SetValue(i, list_value);
	});
}

void XMLScalarFunctions::HTMLExtractTablesJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();

	ExecuteNullSafeString(html_vector, result, count, [&](idx_t i, const std::string &html_string) {
		auto tables = XMLUtils::ExtractHTMLTables(html_string);

		vector<Value> table_values;

		// Process each table
		for (size_t table_idx = 0; table_idx < tables.size(); table_idx++) {
			const auto &table = tables[table_idx];
			const auto &headers = table.headers;
			const auto &rows = table.rows;

			// Create header values
			vector<Value> header_values;
			for (const auto &header : headers) {
				header_values.push_back(Value(header));
			}

			// Create data rows as list of lists
			vector<Value> row_values;
			for (const auto &row : rows) {
				vector<Value> cell_values;
				for (const auto &cell : row) {
					cell_values.push_back(Value(cell));
				}
				row_values.push_back(Value::LIST(cell_values));
			}

			// Build JSON using DuckDB's native JSON construction
			child_list_t<Value> json_children;

			// Headers array
			json_children.push_back({"headers", Value::LIST(header_values)});

			// Data array (2D)
			json_children.push_back({"data", Value::LIST(row_values)});

			// Rows as objects
			vector<Value> object_rows;
			for (const auto &row : rows) {
				child_list_t<Value> row_obj;
				for (size_t j = 0; j < headers.size() && j < row.size(); j++) {
					row_obj.push_back({CompatMakeIdentifier(headers[j]), Value(row[j])});
				}
				object_rows.push_back(Value::STRUCT(row_obj));
			}
			json_children.push_back({"rows", Value::LIST(object_rows)});

			// Metadata
			child_list_t<Value> metadata_children;
			metadata_children.push_back({"line_number", Value::BIGINT(table.line_number)});
			metadata_children.push_back({"num_columns", Value::BIGINT(table.num_columns)});
			metadata_children.push_back({"num_rows", Value::BIGINT(table.num_rows)});
			json_children.push_back({"metadata", Value::STRUCT(metadata_children)});

			Value json_value = Value::STRUCT(json_children);

			// Build structure description
			child_list_t<Value> structure_children;
			structure_children.push_back({"table_name", Value("table_" + std::to_string(table_idx))});

			vector<Value> column_info;
			for (size_t col_idx = 0; col_idx < headers.size(); col_idx++) {
				child_list_t<Value> col_children;
				col_children.push_back({"name", Value(headers[col_idx])});
				col_children.push_back({"index", Value::BIGINT(static_cast<int64_t>(col_idx))});
				col_children.push_back({"type", Value("string")});
				column_info.push_back(Value::STRUCT(col_children));
			}
			structure_children.push_back({"columns", Value::LIST(column_info)});
			structure_children.push_back({"row_count", Value::BIGINT(static_cast<int64_t>(rows.size()))});
			structure_children.push_back({"source_line", Value::BIGINT(table.line_number)});

			Value structure_value = Value::STRUCT(structure_children);

			// Create struct for this table
			child_list_t<Value> table_struct_children;
			table_struct_children.push_back({"table_index", Value::BIGINT(static_cast<int64_t>(table_idx))});
			table_struct_children.push_back({"line_number", Value::BIGINT(table.line_number)});
			table_struct_children.push_back({"num_columns", Value::BIGINT(static_cast<int64_t>(headers.size()))});
			table_struct_children.push_back({"num_rows", Value::BIGINT(static_cast<int64_t>(rows.size()))});
			table_struct_children.push_back({"headers", Value::LIST(header_values)});
			table_struct_children.push_back({"table_data", Value::LIST(row_values)});
			table_struct_children.push_back({"table_json", json_value});
			table_struct_children.push_back({"json_structure", structure_value});

			table_values.push_back(Value::STRUCT(table_struct_children));
		}

		auto table_json_struct_type = LogicalType::STRUCT({
		    make_pair("table_index", LogicalType::BIGINT), make_pair("line_number", LogicalType::BIGINT),
		    make_pair("num_columns", LogicalType::BIGINT), make_pair("num_rows", LogicalType::BIGINT),
		    make_pair("headers", LogicalType::LIST(LogicalType::VARCHAR)),
		    make_pair("table_data", LogicalType::LIST(LogicalType::LIST(LogicalType::VARCHAR))),
		    make_pair("table_json", LogicalType::STRUCT({})),    // Complex nested struct
		    make_pair("json_structure", LogicalType::STRUCT({})) // Complex nested struct
		});

		Value list_value = Value::LIST(table_json_struct_type, table_values);
		result.SetValue(i, list_value);
	});
}

// Elements whose descendant text is whitespace-significant and must be preserved verbatim:
// raw-text elements (script, style) and preformatted elements (pre, textarea).
static bool IsWhitespacePreservingElement(const xmlChar *name) {
	if (!name) {
		return false;
	}
	std::string n(reinterpret_cast<const char *>(name));
	return StringUtil::CIEquals(n, "pre") || StringUtil::CIEquals(n, "textarea") ||
	       StringUtil::CIEquals(n, "script") || StringUtil::CIEquals(n, "style");
}

// Collapse each run of ASCII whitespace in TEXT nodes to a single space, recursively, operating
// on the parse tree before serialization. CDATA and comment nodes are left untouched, and text
// inside whitespace-preserving elements (pre/textarea/script/style) is left verbatim. Working on
// the tree (rather than the serialized string) means a literal '>' or '&' in raw content is never
// mistaken for a tag boundary, and libxml2 still handles escaping, CDATA wrapping and self-closing
// on dump. The rewrite is done in place because the collapsed text is never longer than the
// original, so no reallocation or entity round-trip is required. StringUtil::CharacterIsSpace is
// used so UTF-8 continuation bytes are never misclassified as whitespace.
static void CollapseTextWhitespace(xmlNodePtr node, bool preserve) {
	for (xmlNodePtr child = node->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			CollapseTextWhitespace(child, preserve || IsWhitespacePreservingElement(child->name));
		} else if (child->type == XML_TEXT_NODE && !preserve && child->content) {
			xmlChar *content = child->content;
			size_t w = 0;
			bool pending_space = false;
			for (size_t r = 0; content[r] != '\0'; r++) {
				if (StringUtil::CharacterIsSpace(static_cast<char>(content[r]))) {
					pending_space = true;
					continue;
				}
				if (pending_space) {
					// Keep a single separating space (significant between inline elements)
					content[w++] = ' ';
					pending_space = false;
				}
				content[w++] = content[r];
			}
			if (pending_space) {
				content[w++] = ' ';
			}
			content[w] = '\0';
		}
		// CDATA_SECTION_NODE, COMMENT_NODE, etc. are intentionally left untouched
	}
}

// Serialize a parsed HTML document without the XML declaration or DOCTYPE, collapsing each run of
// whitespace to a single space except inside CDATA sections, comments, and whitespace-preserving
// elements (see CollapseTextWhitespace). Returns false if the document could not be serialized.
static bool SerializeMinifiedHTML(xmlDocPtr doc, std::string &minified_html) {
	// Normalize whitespace on the parse tree first, then let libxml2 serialize.
	CollapseTextWhitespace(reinterpret_cast<xmlNodePtr>(doc), false);

	xmlChar *html_output = nullptr;
	int output_size = 0;
	xmlDocDumpMemory(doc, &html_output, &output_size);
	if (!html_output) {
		return false;
	}
	XMLCharPtr html_ptr(html_output);
	std::string result(reinterpret_cast<const char *>(html_ptr.get()), static_cast<size_t>(output_size));

	// Remove the leading XML declaration if present
	if (result.compare(0, 2, "<?") == 0) {
		size_t xml_decl_end = result.find("?>");
		if (xml_decl_end != std::string::npos) {
			result.erase(0, xml_decl_end + 2);
		}
	}

	// Remove the leading DOCTYPE if present
	size_t content_start = result.find_first_not_of(" \t\n\r");
	if (content_start != std::string::npos && result.compare(content_start, 9, "<!DOCTYPE") == 0) {
		size_t doctype_end = result.find('>', content_start);
		if (doctype_end != std::string::npos) {
			result.erase(content_start, doctype_end - content_start + 1);
		}
	}

	// Trim whitespace introduced at the document edges (e.g. the serializer's trailing newline)
	size_t first = result.find_first_not_of(" \t\n\r");
	if (first == std::string::npos) {
		minified_html.clear();
		return true;
	}
	size_t last = result.find_last_not_of(" \t\n\r");
	minified_html.assign(result, first, last - first + 1);
	return true;
}

void XMLScalarFunctions::ParseHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &file_path_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(file_path_vector, result, args.size(), [&](string_t file_path_str) {
		std::string file_path = file_path_str.GetString();

		try {
			// Read the HTML file using DuckDB's file system
			auto &fs = FileSystem::GetFileSystem(state.GetContext());
			auto file_handle = fs.OpenFile(file_path, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			// Handle empty HTML files gracefully (HTML is more permissive than XML)
			if (file_size == 0) {
				return string_t("<html></html>");
			}

			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

			// Parse the HTML using the HTML parser to normalize it (removes DOCTYPE)
			XMLDocRAII html_doc(content, true); // Use HTML parser
			if (html_doc.IsValid()) {
				std::string minified_html;
				if (SerializeMinifiedHTML(html_doc.doc, minified_html)) {
					return StringVector::AddString(result, minified_html);
				}
			}

			// Fallback to original content if parsing fails
			return StringVector::AddString(result, content);

		} catch (const std::exception &e) {
			throw IOException("Failed to read HTML file '%s': %s", file_path, e.what());
		}
	});
}

void XMLScalarFunctions::ReadHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_content_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(
	    html_content_vector, result, args.size(), [&](string_t html_content_str) {
		    std::string html_content = html_content_str.GetString();

		    // Handle empty HTML content gracefully
		    if (html_content.empty()) {
			    return string_t("<html></html>");
		    }

		    try {
			    // Parse the HTML using the HTML parser to normalize it
			    XMLDocRAII html_doc(html_content, true); // Use HTML parser
			    if (html_doc.IsValid()) {
				    std::string minified_html;
				    if (SerializeMinifiedHTML(html_doc.doc, minified_html)) {
					    return StringVector::AddString(result, minified_html);
				    }
			    }

			    // Fallback to original content if parsing fails
			    return StringVector::AddString(result, html_content);

		    } catch (const std::exception &e) {
			    // Return original content if there's an error parsing
			    return StringVector::AddString(result, html_content);
		    }
	    });
}

void XMLScalarFunctions::HTMLUnescapeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(html_vector, result, args.size(), [&](string_t html_str) {
		std::string html_string = html_str.GetString();
		std::string unescaped = XMLUtils::HTMLUnescape(html_string);
		return StringVector::AddString(result, unescaped);
	});
}

void XMLScalarFunctions::HTMLEscapeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &text_vector = args.data[0];

	UnaryExecutor::Execute<string_t, string_t>(text_vector, result, args.size(), [&](string_t text_str) {
		std::string text = text_str.GetString();
		std::string escaped = XMLUtils::HTMLEscape(text);
		return StringVector::AddString(result, escaped);
	});
}

} // namespace duckdb
