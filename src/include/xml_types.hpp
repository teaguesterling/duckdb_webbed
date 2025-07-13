#pragma once

#include "duckdb.hpp"

namespace duckdb {

class XMLTypes {
public:
	static LogicalType XMLType();
	static LogicalType XMLFragmentType();
	static LogicalType XMLArrayType();
	static bool IsXMLType(const LogicalType& type);
	static bool IsXMLFragmentType(const LogicalType& type);
	static bool IsXMLArrayType(const LogicalType& type);
	static void Register(DatabaseInstance &db);
	
private:
	static bool XMLToVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
	static bool VarcharToXMLCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
	static bool XMLToJSONCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
	static bool JSONToXMLCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb