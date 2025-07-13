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

void XMLScalarFunctions::XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// This will be implemented in Phase 4
	throw NotImplementedException("xml_to_json not yet implemented");
}

void XMLScalarFunctions::ValueToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// This will be implemented in Phase 4
	throw NotImplementedException("value_to_xml not yet implemented");
}

void XMLScalarFunctions::XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// This will be implemented in Phase 4
	throw NotImplementedException("xml_stats not yet implemented");
}

void XMLScalarFunctions::XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// This will be implemented in Phase 4
	throw NotImplementedException("xml_namespaces not yet implemented");
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
}

} // namespace duckdb