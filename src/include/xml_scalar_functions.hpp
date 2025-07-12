#pragma once

#include "duckdb.hpp"

namespace duckdb {

class XMLScalarFunctions {
public:
	static void Register(DatabaseInstance &db);
	
private:
	// Validation functions
	static void XMLValidFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLWellFormedFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Text extraction functions
	static void XMLExtractTextFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractAllTextFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Element extraction functions
	static void XMLExtractElementsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractAttributesFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Conversion functions
	static void XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void ValueToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Analysis functions
	static void XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb