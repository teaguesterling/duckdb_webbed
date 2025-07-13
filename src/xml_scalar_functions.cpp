#include "xml_scalar_functions.hpp"
#include "xml_utils.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

namespace duckdb {

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
		xml_vector, xpath_vector, result, args.size(),
		[&](string_t xml_str, string_t xpath_str) {
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
	auto count = args.size();
	
	// For complex types like LIST<STRUCT>, we need to manually handle the vectorization
	for (idx_t i = 0; i < count; i++) {
		// Get input values
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		auto xpath_str = FlatVector::GetData<string_t>(xpath_vector)[i];
		
		std::string xml_string = xml_str.GetString();
		std::string xpath_string = xpath_str.GetString();
		
		// Extract elements using XPath
		auto elements = XMLUtils::ExtractByXPath(xml_string, xpath_string);
		
		// Create list of structs
		vector<Value> struct_values;
		
		for (const auto &elem : elements) {
			// Create struct with fields: name, text_content, namespace_uri, path, line_number
			child_list_t<Value> struct_children;
			struct_children.emplace_back("name", Value(elem.name));
			struct_children.emplace_back("text_content", Value(elem.text_content));
			struct_children.emplace_back("namespace_uri", Value(elem.namespace_uri));
			struct_children.emplace_back("path", Value(elem.path));
			struct_children.emplace_back("line_number", Value::BIGINT(elem.line_number));
			
			struct_values.emplace_back(Value::STRUCT(struct_children));
		}
		
		// Create list value
		auto struct_type = LogicalType::STRUCT({
			make_pair("name", LogicalType::VARCHAR),
			make_pair("text_content", LogicalType::VARCHAR),
			make_pair("namespace_uri", LogicalType::VARCHAR),
			make_pair("path", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(struct_type, struct_values);
		
		// Set result
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::XMLExtractAttributesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	auto count = args.size();
	
	// Extract attributes from elements matching XPath expression
	for (idx_t i = 0; i < count; i++) {
		// Get input values
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		auto xpath_str = FlatVector::GetData<string_t>(xpath_vector)[i];
		
		std::string xml_string = xml_str.GetString();
		std::string xpath_string = xpath_str.GetString();
		
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
		
		// Create list value
		auto attr_struct_type = LogicalType::STRUCT({
			make_pair("element_name", LogicalType::VARCHAR),
			make_pair("element_path", LogicalType::VARCHAR),
			make_pair("attribute_name", LogicalType::VARCHAR),
			make_pair("attribute_value", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(attr_struct_type, attr_values);
		
		// Set result
		result.SetValue(i, list_value);
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
	
	BinaryExecutor::Execute<string_t, string_t, bool>(
		xml_vector, schema_vector, result, args.size(),
		[&](string_t xml_str, string_t schema_str) {
			std::string xml_string = xml_str.GetString();
			std::string schema_string = schema_str.GetString();
			return XMLUtils::ValidateXMLSchema(xml_string, schema_string);
		});
}

void XMLScalarFunctions::XMLExtractCommentsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		std::string xml_string = xml_str.GetString();
		
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
		auto comment_struct_type = LogicalType::STRUCT({
			make_pair("content", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(comment_struct_type, comment_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::XMLExtractCDataFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		std::string xml_string = xml_str.GetString();
		
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
		auto cdata_struct_type = LogicalType::STRUCT({
			make_pair("content", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(cdata_struct_type, cdata_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		std::string xml_string = xml_str.GetString();
		
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
	}
}

void XMLScalarFunctions::XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto xml_str = FlatVector::GetData<string_t>(xml_vector)[i];
		std::string xml_string = xml_str.GetString();
		
		auto namespaces = XMLUtils::ExtractNamespaces(xml_string);
		
		// Create list of namespace structs
		vector<Value> ns_values;
		
		for (const auto &ns : namespaces) {
			child_list_t<Value> ns_children;
			ns_children.emplace_back("prefix", Value(ns.prefix));
			ns_children.emplace_back("uri", Value(ns.uri));
			
			ns_values.emplace_back(Value::STRUCT(ns_children));
		}
		
		// Create list value
		auto ns_struct_type = LogicalType::STRUCT({
			make_pair("prefix", LogicalType::VARCHAR),
			make_pair("uri", LogicalType::VARCHAR)
		});
		
		Value list_value = Value::LIST(ns_struct_type, ns_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	
	UnaryExecutor::Execute<string_t, string_t>(xml_vector, result, args.size(), [&](string_t xml_str) {
		std::string xml_string = xml_str.GetString();
		std::string json_string = XMLUtils::XMLToJSON(xml_string);
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
	// This will be implemented later
	throw NotImplementedException("value_to_xml not yet implemented");
}

void XMLScalarFunctions::Register(DatabaseInstance &db) {
	// Register legacy xml function for compatibility
	auto xml_function = ScalarFunction("xml", {LogicalType::VARCHAR}, LogicalType::VARCHAR, 
		[](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &name_vector = args.data[0];
			UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
				return StringVector::AddString(result, "Xml " + name.GetString() + " 🐥");
			});
		});
	ExtensionUtil::RegisterFunction(db, xml_function);
	
	// Register xml_libxml2_version function 
	auto xml_libxml2_version_function = ScalarFunction("xml_libxml2_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
		[](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &name_vector = args.data[0];
			UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
				return StringVector::AddString(result, "Xml " + name.GetString() + ", my linked libxml2 version is 2.13.8");
			});
		});
	ExtensionUtil::RegisterFunction(db, xml_libxml2_version_function);
	
	// Register xml_valid function
	auto xml_valid_function = ScalarFunction("xml_valid", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidFunction);
	ExtensionUtil::RegisterFunction(db, xml_valid_function);
	
	// Register xml_well_formed function
	auto xml_well_formed_function = ScalarFunction("xml_well_formed", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	ExtensionUtil::RegisterFunction(db, xml_well_formed_function);
	
	// Register xml_extract_text function
	auto xml_extract_text_function = ScalarFunction("xml_extract_text", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_text_function);
	
	// Register xml_extract_all_text function
	auto xml_extract_all_text_function = ScalarFunction("xml_extract_all_text", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_all_text_function);
	
	// Register xml_extract_elements function (returns LIST<STRUCT>)
	auto element_struct_type = LogicalType::STRUCT({
		make_pair("name", LogicalType::VARCHAR),
		make_pair("text_content", LogicalType::VARCHAR),
		make_pair("namespace_uri", LogicalType::VARCHAR),
		make_pair("path", LogicalType::VARCHAR),
		make_pair("line_number", LogicalType::BIGINT)
	});
	auto xml_extract_elements_function = ScalarFunction("xml_extract_elements", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::LIST(element_struct_type), XMLExtractElementsFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_elements_function);
	
	// Register xml_extract_attributes function (returns LIST<STRUCT>)
	auto attr_struct_type = LogicalType::STRUCT({
		make_pair("element_name", LogicalType::VARCHAR),
		make_pair("element_path", LogicalType::VARCHAR),
		make_pair("attribute_name", LogicalType::VARCHAR),
		make_pair("attribute_value", LogicalType::VARCHAR),
		make_pair("line_number", LogicalType::BIGINT)
	});
	auto xml_extract_attributes_function = ScalarFunction("xml_extract_attributes", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::LIST(attr_struct_type), XMLExtractAttributesFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_attributes_function);
	
	// Register xml_pretty_print function
	auto xml_pretty_print_function = ScalarFunction("xml_pretty_print", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLPrettyPrintFunction);
	ExtensionUtil::RegisterFunction(db, xml_pretty_print_function);
	
	// Register xml_minify function
	auto xml_minify_function = ScalarFunction("xml_minify", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLMinifyFunction);
	ExtensionUtil::RegisterFunction(db, xml_minify_function);
	
	// Register xml_validate_schema function
	auto xml_validate_schema_function = ScalarFunction("xml_validate_schema", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidateSchemaFunction);
	ExtensionUtil::RegisterFunction(db, xml_validate_schema_function);
	
	// Register xml_extract_comments function (returns LIST<STRUCT>)
	auto comment_struct_type = LogicalType::STRUCT({
		make_pair("content", LogicalType::VARCHAR),
		make_pair("line_number", LogicalType::BIGINT)
	});
	auto xml_extract_comments_function = ScalarFunction("xml_extract_comments", 
		{LogicalType::VARCHAR}, LogicalType::LIST(comment_struct_type), XMLExtractCommentsFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_comments_function);
	
	// Register xml_extract_cdata function (returns LIST<STRUCT>)
	auto xml_extract_cdata_function = ScalarFunction("xml_extract_cdata", 
		{LogicalType::VARCHAR}, LogicalType::LIST(comment_struct_type), XMLExtractCDataFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_cdata_function);
	
	// Register xml_stats function (returns STRUCT)
	auto stats_struct_type = LogicalType::STRUCT({
		make_pair("element_count", LogicalType::BIGINT),
		make_pair("attribute_count", LogicalType::BIGINT),
		make_pair("max_depth", LogicalType::BIGINT),
		make_pair("size_bytes", LogicalType::BIGINT),
		make_pair("namespace_count", LogicalType::BIGINT)
	});
	auto xml_stats_function = ScalarFunction("xml_stats", 
		{LogicalType::VARCHAR}, stats_struct_type, XMLStatsFunction);
	ExtensionUtil::RegisterFunction(db, xml_stats_function);
	
	// Register xml_namespaces function (returns LIST<STRUCT>)
	auto namespace_struct_type = LogicalType::STRUCT({
		make_pair("prefix", LogicalType::VARCHAR),
		make_pair("uri", LogicalType::VARCHAR)
	});
	auto xml_namespaces_function = ScalarFunction("xml_namespaces", 
		{LogicalType::VARCHAR}, LogicalType::LIST(namespace_struct_type), XMLNamespacesFunction);
	ExtensionUtil::RegisterFunction(db, xml_namespaces_function);
	
	// Register xml_to_json function
	auto xml_to_json_function = ScalarFunction("xml_to_json", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLToJSONFunction);
	ExtensionUtil::RegisterFunction(db, xml_to_json_function);
	
	// Register json_to_xml function
	auto json_to_xml_function = ScalarFunction("json_to_xml", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, JSONToXMLFunction);
	ExtensionUtil::RegisterFunction(db, json_to_xml_function);
}

} // namespace duckdb