#include "xml_scalar_functions.hpp"
#include "xml_utils.hpp"
#include "xml_types.hpp"
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
	
	BinaryExecutor::Execute<string_t, string_t, string_t>(
		xml_vector, xpath_vector, result, args.size(),
		[&](string_t xml_str, string_t xpath_str) {
			std::string xml_string = xml_str.GetString();
			std::string xpath_string = xpath_str.GetString();
			
			// Extract XML fragment using our new utility function
			std::string fragment_xml = XMLUtils::ExtractXMLFragment(xml_string, xpath_string);
			
			return StringVector::AddString(result, fragment_xml);
		});
}

void XMLScalarFunctions::XMLExtractElementsStringFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &xml_vector = args.data[0];
	auto &xpath_vector = args.data[1];
	
	BinaryExecutor::Execute<string_t, string_t, string_t>(
		xml_vector, xpath_vector, result, args.size(),
		[&](string_t xml_str, string_t xpath_str) {
			std::string xml_string = xml_str.GetString();
			std::string xpath_string = xpath_str.GetString();
			
			// Extract XML fragment using our new utility function
			std::string fragment_xml = XMLUtils::ExtractXMLFragment(xml_string, xpath_string);
			
			return StringVector::AddString(result, fragment_xml);
		});
}

void XMLScalarFunctions::XMLWrapFragmentFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &fragment_vector = args.data[0];
	auto &wrapper_vector = args.data[1];
	
	BinaryExecutor::Execute<string_t, string_t, string_t>(
		fragment_vector, wrapper_vector, result, args.size(),
		[&](string_t fragment_str, string_t wrapper_str) {
			std::string fragment = fragment_str.GetString();
			std::string wrapper = wrapper_str.GetString();
			
			// Create wrapped XML: <wrapper>fragment</wrapper>
			std::string wrapped_xml = "<" + wrapper + ">" + fragment + "</" + wrapper + ">";
			
			return StringVector::AddString(result, wrapped_xml);
		});
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
			is_json_type = (input_type.id() == LogicalTypeId::VARCHAR && 
							input_type.HasAlias() && 
							input_type.GetAlias() == "JSON");
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
					input_str = "";
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

void XMLScalarFunctions::Register(DatabaseInstance &db) {
	// Register xml function (same as to_xml for now) - using VARCHAR for now, will enhance type system later
	auto xml_function = ScalarFunction("xml", {LogicalType::VARCHAR}, LogicalType::VARCHAR, ValueToXMLFunction);
	ExtensionUtil::RegisterFunction(db, xml_function);
	
	// Register to_xml function (single argument) - ANY type variant (unified path)
	auto to_xml_any_function = ScalarFunction("to_xml", {LogicalType::ANY}, XMLTypes::XMLType(), ValueToXMLFunction);
	ExtensionUtil::RegisterFunction(db, to_xml_any_function);
	
	// Register to_xml function (two arguments: value, node_name) - ANY type variant (unified path)
	auto to_xml_any_with_name_function = ScalarFunction("to_xml", 
		{LogicalType::ANY, LogicalType::VARCHAR}, XMLTypes::XMLType(), ValueToXMLFunction);
	ExtensionUtil::RegisterFunction(db, to_xml_any_with_name_function);
	
	// Register xml_libxml2_version function 
	auto xml_libxml2_version_function = ScalarFunction("xml_libxml2_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
		[](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &name_vector = args.data[0];
			UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
				return StringVector::AddString(result, "Xml " + name.GetString() + ", my linked libxml2 version is 2.13.8");
			});
		});
	ExtensionUtil::RegisterFunction(db, xml_libxml2_version_function);
	
	// Register xml_valid function - both XML and VARCHAR overloads
	auto xml_valid_function = ScalarFunction("xml_valid", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLValidFunction);
	ExtensionUtil::RegisterFunction(db, xml_valid_function);
	auto xml_valid_varchar_function = ScalarFunction("xml_valid", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidFunction);
	ExtensionUtil::RegisterFunction(db, xml_valid_varchar_function);
	
	// Register xml_well_formed function - both XML and VARCHAR overloads
	auto xml_well_formed_function = ScalarFunction("xml_well_formed", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	ExtensionUtil::RegisterFunction(db, xml_well_formed_function);
	auto xml_well_formed_varchar_function = ScalarFunction("xml_well_formed", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	ExtensionUtil::RegisterFunction(db, xml_well_formed_varchar_function);
	
	// Register xml_extract_text function with multiple overloads to handle string literals
	ScalarFunctionSet xml_extract_text_functions("xml_extract_text");
	
	// XML + VARCHAR
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// XML + STRING_LITERAL (for direct string literals)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// XMLFragment + VARCHAR (for results from xml_extract_elements)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// XMLFragment + STRING_LITERAL
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// VARCHAR + VARCHAR (compatibility)
	xml_extract_text_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// VARCHAR + STRING_LITERAL (compatibility)
	xml_extract_text_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	
	ExtensionUtil::RegisterFunction(db, xml_extract_text_functions);
	
	// Register xml_extract_all_text function - both XML and VARCHAR overloads
	auto xml_extract_all_text_function = ScalarFunction("xml_extract_all_text", 
		{XMLTypes::XMLType()}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_all_text_function);
	auto xml_extract_all_text_varchar_function = ScalarFunction("xml_extract_all_text", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_all_text_varchar_function);
	
	// Register xml_extract_elements function as a function set
	ScalarFunctionSet xml_extract_elements_functions("xml_extract_elements");
	
	// XML + VARCHAR
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XML + STRING_LITERAL
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XMLFragment + VARCHAR (for nested extraction)
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XMLFragment + STRING_LITERAL
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// VARCHAR + VARCHAR (compatibility)
	xml_extract_elements_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// VARCHAR + STRING_LITERAL (compatibility)
	xml_extract_elements_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	
	ExtensionUtil::RegisterFunction(db, xml_extract_elements_functions);
	
	// Register xml_extract_elements_string function as a function set
	ScalarFunctionSet xml_extract_elements_string_functions("xml_extract_elements_string");
	xml_extract_elements_string_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractElementsStringFunction));
	xml_extract_elements_string_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractElementsStringFunction));
	ExtensionUtil::RegisterFunction(db, xml_extract_elements_string_functions);
	
	// Register xml_wrap_fragment function (returns XML)
	auto xml_wrap_fragment_function = ScalarFunction("xml_wrap_fragment", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, XMLTypes::XMLType(), XMLWrapFragmentFunction);
	ExtensionUtil::RegisterFunction(db, xml_wrap_fragment_function);
	
	// Register xml_extract_attributes function (returns LIST<STRUCT>)
	auto attr_struct_type = LogicalType::STRUCT({
		make_pair("element_name", LogicalType::VARCHAR),
		make_pair("element_path", LogicalType::VARCHAR),
		make_pair("attribute_name", LogicalType::VARCHAR),
		make_pair("attribute_value", LogicalType::VARCHAR),
		make_pair("line_number", LogicalType::BIGINT)
	});
	// Register xml_extract_attributes function as a function set
	ScalarFunctionSet xml_extract_attributes_functions("xml_extract_attributes");
	xml_extract_attributes_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::LIST(attr_struct_type), XMLExtractAttributesFunction));
	xml_extract_attributes_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::LIST(attr_struct_type), XMLExtractAttributesFunction));
	ExtensionUtil::RegisterFunction(db, xml_extract_attributes_functions);
	
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
		{XMLTypes::XMLType()}, LogicalType::LIST(comment_struct_type), XMLExtractCommentsFunction);
	ExtensionUtil::RegisterFunction(db, xml_extract_comments_function);
	
	// Register xml_extract_cdata function (returns LIST<STRUCT>)
	auto xml_extract_cdata_function = ScalarFunction("xml_extract_cdata", 
		{XMLTypes::XMLType()}, LogicalType::LIST(comment_struct_type), XMLExtractCDataFunction);
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