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
	static void XMLValidateSchemaFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Text extraction functions
	static void XMLExtractTextFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractAllTextFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Element extraction functions
	static void XMLExtractElementsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractElementsStringFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLWrapFragmentFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractAttributesFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractCommentsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLExtractCDataFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Content manipulation functions
	static void XMLPrettyPrintFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLMinifyFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Conversion functions
	static void XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void JSONToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void ValueToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// Analysis functions
	static void XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// HTML-specific extraction functions
	static void HTMLExtractTextFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void HTMLExtractTextWithXPathFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void HTMLExtractLinksFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void HTMLExtractImagesFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void HTMLExtractTableRowsFunction(DataChunk &args, ExpressionState &state, Vector &result);
	static void HTMLExtractTablesJSONFunction(DataChunk &args, ExpressionState &state, Vector &result);
	
	// HTML file parsing functions
	static void ParseHTMLFunction(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb