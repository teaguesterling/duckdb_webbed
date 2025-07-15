#include "xml_types.hpp"
#include "xml_utils.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/type_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_type_info.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension_util.hpp"

namespace duckdb {

LogicalType XMLTypes::XMLType() {
	auto xml_type = LogicalType(LogicalTypeId::VARCHAR);
	xml_type.SetAlias("XML");
	return xml_type;
}

LogicalType XMLTypes::XMLFragmentType() {
	auto xml_frag_type = LogicalType(LogicalTypeId::VARCHAR);
	xml_frag_type.SetAlias("xmlfragment");
	return xml_frag_type;
}

LogicalType XMLTypes::XMLArrayType() {
	return LogicalType::LIST(XMLType());
}

bool XMLTypes::IsXMLType(const LogicalType& type) {
	return type.id() == LogicalTypeId::VARCHAR && type.HasAlias() && type.GetAlias() == "XML";
}

bool XMLTypes::IsXMLFragmentType(const LogicalType& type) {
	return type.id() == LogicalTypeId::VARCHAR && type.HasAlias() && type.GetAlias() == "xmlfragment";
}

bool XMLTypes::IsXMLArrayType(const LogicalType& type) {
	return type.id() == LogicalTypeId::LIST && IsXMLType(ListType::GetChildType(type));
}

bool XMLTypes::XMLToVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	// XML to VARCHAR is essentially a no-op since XML is stored as VARCHAR internally
	VectorOperations::Copy(source, result, count, 0, 0);
	return true;
}

bool XMLTypes::VarcharToXMLCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	// VARCHAR to XML: validate the XML if strict casting is enabled
	// For now, just copy the data - validation will be done by xml_valid() function
	VectorOperations::Copy(source, result, count, 0, 0);
	return true;
}

bool XMLTypes::XMLToJSONCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	// This will be implemented in Phase 4 - XML to JSON conversion
	// For now, throw an error
	throw NotImplementedException("XML to JSON conversion not yet implemented");
}

bool XMLTypes::JSONToXMLCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	// Convert JSON to XML using our existing JSONToXML utility
	UnaryExecutor::Execute<string_t, string_t>(source, result, count, [&](string_t json_input) {
		std::string json_str = json_input.GetString();
		std::string xml_result = XMLUtils::JSONToXML(json_str);
		return StringVector::AddString(result, xml_result);
	});
	return true;
}

void XMLTypes::Register(DatabaseInstance &db) {
	// For now, register XML as a simple type alias
	// This creates a user-defined type that acts like VARCHAR but with the name "XML"
	
	auto xml_type = LogicalType(LogicalType::VARCHAR);
	xml_type.SetAlias("XML");
	
	// Register the XML type through the extension utility
	ExtensionUtil::RegisterType(db, "XML", xml_type);
	
	// Register XMLFragment type
	auto xml_fragment_type = LogicalType(LogicalType::VARCHAR);
	xml_fragment_type.SetAlias("XMLFragment");
	ExtensionUtil::RegisterType(db, "XMLFragment", xml_fragment_type);
	
	// Register cast functions for XML type conversion
	ExtensionUtil::RegisterCastFunction(db, LogicalType::VARCHAR, xml_type, VarcharToXMLCast);
	ExtensionUtil::RegisterCastFunction(db, xml_type, LogicalType::VARCHAR, XMLToVarcharCast);
	
	// Register cast functions for XMLFragment type conversion
	ExtensionUtil::RegisterCastFunction(db, LogicalType::VARCHAR, xml_fragment_type, VarcharToXMLCast);
	ExtensionUtil::RegisterCastFunction(db, xml_fragment_type, LogicalType::VARCHAR, XMLToVarcharCast);
	ExtensionUtil::RegisterCastFunction(db, xml_fragment_type, xml_type, VarcharToXMLCast);
	
	// Register JSON to XML cast (JSON extension is loaded during XML extension initialization)
	try {
		auto json_type = LogicalType::JSON();
		ExtensionUtil::RegisterCastFunction(db, json_type, xml_type, JSONToXMLCast);
	} catch (...) {
		// JSON type might not be available, but this shouldn't prevent XML extension from loading
	}
}

} // namespace duckdb