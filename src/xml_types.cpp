#include "xml_types.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/type_catalog_entry.hpp"
#include "duckdb/parser/parsed_data/create_type_info.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"

namespace duckdb {

LogicalType XMLTypes::XMLType() {
	auto xml_type = LogicalType(LogicalTypeId::VARCHAR);
	xml_type.SetAlias("xml");
	return xml_type;
}

LogicalType XMLTypes::XMLFragmentType() {
	auto xml_frag_type = LogicalType(LogicalTypeId::VARCHAR);
	xml_frag_type.SetAlias("xmlfragment");
	return xml_frag_type;
}

bool XMLTypes::IsXMLType(const LogicalType& type) {
	return type.id() == LogicalTypeId::VARCHAR && type.HasAlias() && type.GetAlias() == "xml";
}

bool XMLTypes::IsXMLFragmentType(const LogicalType& type) {
	return type.id() == LogicalTypeId::VARCHAR && type.HasAlias() && type.GetAlias() == "xmlfragment";
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

void XMLTypes::Register(DatabaseInstance &db) {
	// For now, we'll skip creating a custom XML type and just use VARCHAR directly
	// This simplifies the implementation while maintaining functionality
	// The XML type validation will be done through the xml_valid() function
	
	// Register cast functions
	// These will be needed for proper type system integration
	// For now, we'll rely on implicit VARCHAR casting
}

} // namespace duckdb