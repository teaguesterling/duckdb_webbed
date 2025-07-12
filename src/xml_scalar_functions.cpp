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
	// For now, implement a simplified version that returns a JSON-like string representation
	// This avoids the complex LIST<STRUCT> type handling issues
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	
	BinaryExecutor::Execute<string_t, string_t, string_t>(
		xml_vector, xpath_vector, result, args.size(),
		[&](string_t xml_str, string_t xpath_str) {
			std::string xml_string = xml_str.GetString();
			std::string xpath_string = xpath_str.GetString();
			
			auto elements = XMLUtils::ExtractByXPath(xml_string, xpath_string);
			
			// Convert to a simple JSON-like string representation for now
			std::string json_result = "[";
			for (size_t i = 0; i < elements.size(); i++) {
				if (i > 0) json_result += ", ";
				const auto &elem = elements[i];
				json_result += "{\"name\": \"" + elem.name + "\", ";
				json_result += "\"text_content\": \"" + elem.text_content + "\", ";
				json_result += "\"namespace_uri\": \"" + elem.namespace_uri + "\", ";
				json_result += "\"path\": \"" + elem.path + "\", ";
				json_result += "\"line_number\": " + std::to_string(elem.line_number) + "}";
			}
			json_result += "]";
			
			return StringVector::AddString(result, json_result);
		});
}

void XMLScalarFunctions::XMLExtractAttributesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// This will extract attributes from elements matching a path
	// Implementation will be similar to XMLExtractElementsFunction but focused on attributes
	throw NotImplementedException("xml_extract_attributes not yet fully implemented");
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
	
	// Register xml_extract_elements function (returns JSON-like string for now)
	auto xml_extract_elements_function = ScalarFunction("xml_extract_elements", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractElementsFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_elements_function);
}

} // namespace duckdb