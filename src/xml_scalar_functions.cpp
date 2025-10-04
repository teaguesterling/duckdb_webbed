#include "xml_scalar_functions.hpp"
#include "xml_utils.hpp"
#include "xml_types.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

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
			
			// Extract ALL XML fragments separated by newlines
			std::string fragment_xml = XMLUtils::ExtractXMLFragmentAll(xml_string, xpath_string);
			
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

unique_ptr<FunctionData> XMLScalarFunctions::XMLToJSONWithSchemaBind(ClientContext &context, ScalarFunction &bound_function, vector<unique_ptr<Expression>> &arguments) {
	if (arguments.empty()) {
		throw BinderException("xml_to_json requires at least one argument (the XML string)");
	}
	
	XMLToJSONOptions options; // Start with defaults
	
	// First argument must be the XML string (no alias)
	if (!arguments[0]->GetAlias().empty()) {
		throw BinderException("First argument to xml_to_json must be the XML string (without named parameter)");
	}
	
	// Process named parameters (if any)
	for (idx_t i = 1; i < arguments.size(); i++) {
		auto &arg = arguments[i];
		std::string param_name = arg->GetAlias();
		
		if (param_name.empty()) {
			throw BinderException("All arguments after the first must be named parameters (e.g., force_list := ['name'])");
		}
		
		// Check if the argument is foldable (constant)
		if (arg->HasParameter()) {
			throw ParameterNotResolvedException();
		}
		if (!arg->IsFoldable()) {
			throw BinderException("Parameter '%s' must be a constant value", param_name);
		}
		
		// Extract the constant value
		Value param_value = ExpressionExecutor::EvaluateScalar(context, *arg);
		
		if (param_name == "force_list") {
			if (param_value.IsNull()) {
				options.force_list.clear(); // NULL means empty list
			} else if (param_value.type().id() != LogicalTypeId::LIST) {
				throw BinderException("force_list parameter must be a list of strings, e.g., ['name', 'item']");
			} else {
				// Check child type only if list is not empty
				auto &list_children = ListValue::GetChildren(param_value);
				if (!list_children.empty() && ListType::GetChildType(param_value.type()).id() != LogicalTypeId::VARCHAR) {
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
		} else if (param_name == "strip_namespaces") {
			if (param_value.IsNull()) {
				options.strip_namespaces = true; // Default
			} else if (param_value.type().id() != LogicalTypeId::BOOLEAN) {
				throw BinderException("strip_namespaces parameter must be a boolean");
			} else {
				options.strip_namespaces = BooleanValue::Get(param_value);
			}
		} else if (param_name == "empty_elements") {
			if (param_value.IsNull()) {
				options.empty_elements = "object"; // Default
			} else if (param_value.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("empty_elements parameter must be a string ('object', 'null', or 'string')");
			} else {
				std::string empty_val = StringValue::Get(param_value);
				if (empty_val != "object" && empty_val != "null" && empty_val != "string") {
					throw BinderException("empty_elements must be 'object', 'null', or 'string', got '%s'", empty_val);
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
	auto &bind_data = func_expr.bind_info->Cast<XMLToJSONBindData>();
	auto &xml_vector = args.data[0];
	
	// All options were extracted at bind time, just use them
	const XMLToJSONOptions &options = bind_data.options;
	
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

void XMLScalarFunctions::Register(ExtensionLoader &loader) {
	// Register xml function (same as to_xml for now) - using VARCHAR for now, will enhance type system later
	auto xml_function = ScalarFunction("xml", {LogicalType::VARCHAR}, LogicalType::VARCHAR, ValueToXMLFunction);
	loader.RegisterFunction(xml_function);
	
	// Register to_xml function (single argument) - ANY type variant (unified path)
	auto to_xml_any_function = ScalarFunction("to_xml", {LogicalType::ANY}, XMLTypes::XMLType(), ValueToXMLFunction);
	loader.RegisterFunction(to_xml_any_function);
	
	// Register to_xml function (two arguments: value, node_name) - ANY type variant (unified path)
	auto to_xml_any_with_name_function = ScalarFunction("to_xml", 
		{LogicalType::ANY, LogicalType::VARCHAR}, XMLTypes::XMLType(), ValueToXMLFunction);
	loader.RegisterFunction(to_xml_any_with_name_function);
	
	// Register xml_libxml2_version function 
	auto xml_libxml2_version_function = ScalarFunction("xml_libxml2_version", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
		[](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &name_vector = args.data[0];
			UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
				return StringVector::AddString(result, "Xml " + name.GetString() + ", my linked libxml2 version is 2.13.8");
			});
		});
	loader.RegisterFunction(xml_libxml2_version_function);
	
	// Register xml_valid function - both XML and VARCHAR overloads
	auto xml_valid_function = ScalarFunction("xml_valid", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLValidFunction);
	loader.RegisterFunction(xml_valid_function);
	auto xml_valid_varchar_function = ScalarFunction("xml_valid", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidFunction);
	loader.RegisterFunction(xml_valid_varchar_function);
	
	// Register xml_well_formed function - both XML and VARCHAR overloads
	auto xml_well_formed_function = ScalarFunction("xml_well_formed", {XMLTypes::XMLType()}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	loader.RegisterFunction(xml_well_formed_function);
	auto xml_well_formed_varchar_function = ScalarFunction("xml_well_formed", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLWellFormedFunction);
	loader.RegisterFunction(xml_well_formed_varchar_function);
	
	// Register xml_extract_text function with multiple overloads to handle string literals
	ScalarFunctionSet xml_extract_text_functions("xml_extract_text");
	
	// XML + VARCHAR
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// XML + STRING_LITERAL (for direct string literals)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// HTMLType + VARCHAR (use HTML-specific function)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, HTMLExtractTextWithXPathFunction));
	// HTMLType + STRING_LITERAL (use HTML-specific function)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, HTMLExtractTextWithXPathFunction));
	// XMLFragment + VARCHAR (for results from xml_extract_elements)
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// XMLFragment + STRING_LITERAL
	xml_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// VARCHAR + VARCHAR (compatibility)
	xml_extract_text_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractTextFunction));
	// VARCHAR + STRING_LITERAL (compatibility)
	xml_extract_text_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, XMLExtractTextFunction));
	
	loader.RegisterFunction(xml_extract_text_functions);
	
	// Register xml_extract_all_text function - both XML and VARCHAR overloads
	auto xml_extract_all_text_function = ScalarFunction("xml_extract_all_text", 
		{XMLTypes::XMLType()}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	loader.RegisterFunction(xml_extract_all_text_function);
	auto xml_extract_all_text_varchar_function = ScalarFunction("xml_extract_all_text", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractAllTextFunction);
	loader.RegisterFunction(xml_extract_all_text_varchar_function);
	
	// Register xml_extract_elements function as a function set
	ScalarFunctionSet xml_extract_elements_functions("xml_extract_elements");
	
	// XML + VARCHAR
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XML + STRING_LITERAL
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// HTML + VARCHAR
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// HTML + STRING_LITERAL
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XMLFragment + VARCHAR (for nested extraction)
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// XMLFragment + STRING_LITERAL
	xml_extract_elements_functions.AddFunction(ScalarFunction({XMLTypes::XMLFragmentType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// VARCHAR + VARCHAR (compatibility)
	xml_extract_elements_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	// VARCHAR + STRING_LITERAL (compatibility)
	xml_extract_elements_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType(LogicalTypeId::STRING_LITERAL)}, XMLTypes::XMLFragmentType(), XMLExtractElementsFunction));
	
	loader.RegisterFunction(xml_extract_elements_functions);
	
	// Register xml_extract_elements_string function as a function set
	ScalarFunctionSet xml_extract_elements_string_functions("xml_extract_elements_string");
	xml_extract_elements_string_functions.AddFunction(ScalarFunction({XMLTypes::XMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractElementsStringFunction));
	xml_extract_elements_string_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLExtractElementsStringFunction));
	loader.RegisterFunction(xml_extract_elements_string_functions);
	
	// Register xml_wrap_fragment function (returns XML)
	auto xml_wrap_fragment_function = ScalarFunction("xml_wrap_fragment", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, XMLTypes::XMLType(), XMLWrapFragmentFunction);
	loader.RegisterFunction(xml_wrap_fragment_function);
	
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
	xml_extract_attributes_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR}, LogicalType::LIST(attr_struct_type), XMLExtractAttributesFunction));
	xml_extract_attributes_functions.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::LIST(attr_struct_type), XMLExtractAttributesFunction));
	loader.RegisterFunction(xml_extract_attributes_functions);
	
	// Register xml_pretty_print function
	auto xml_pretty_print_function = ScalarFunction("xml_pretty_print", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLPrettyPrintFunction);
	loader.RegisterFunction(xml_pretty_print_function);
	
	// Register xml_minify function
	auto xml_minify_function = ScalarFunction("xml_minify", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLMinifyFunction);
	loader.RegisterFunction(xml_minify_function);
	
	// Register xml_validate_schema function
	auto xml_validate_schema_function = ScalarFunction("xml_validate_schema", 
		{LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::BOOLEAN, XMLValidateSchemaFunction);
	loader.RegisterFunction(xml_validate_schema_function);
	
	// Register xml_extract_comments function (returns LIST<STRUCT>)
	auto comment_struct_type = LogicalType::STRUCT({
		make_pair("content", LogicalType::VARCHAR),
		make_pair("line_number", LogicalType::BIGINT)
	});
	auto xml_extract_comments_function = ScalarFunction("xml_extract_comments", 
		{XMLTypes::XMLType()}, LogicalType::LIST(comment_struct_type), XMLExtractCommentsFunction);
	loader.RegisterFunction(xml_extract_comments_function);
	
	// Register xml_extract_cdata function (returns LIST<STRUCT>)
	auto xml_extract_cdata_function = ScalarFunction("xml_extract_cdata", 
		{XMLTypes::XMLType()}, LogicalType::LIST(comment_struct_type), XMLExtractCDataFunction);
	loader.RegisterFunction(xml_extract_cdata_function);
	
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
	loader.RegisterFunction(xml_stats_function);
	
	// Register xml_namespaces function (returns LIST<STRUCT>)
	auto namespace_struct_type = LogicalType::STRUCT({
		make_pair("prefix", LogicalType::VARCHAR),
		make_pair("uri", LogicalType::VARCHAR)
	});
	auto xml_namespaces_function = ScalarFunction("xml_namespaces", 
		{LogicalType::VARCHAR}, LogicalType::LIST(namespace_struct_type), XMLNamespacesFunction);
	loader.RegisterFunction(xml_namespaces_function);
	
	// Register xml_to_json function with optional named parameters
	ScalarFunction xml_to_json_function("xml_to_json", {LogicalType::VARCHAR}, LogicalType::VARCHAR, XMLToJSONWithSchemaFunction, XMLToJSONWithSchemaBind);
	xml_to_json_function.varargs = LogicalType::ANY;
	loader.RegisterFunction(xml_to_json_function);
	
	// Register json_to_xml function
	auto json_to_xml_function = ScalarFunction("json_to_xml", 
		{LogicalType::VARCHAR}, LogicalType::VARCHAR, JSONToXMLFunction);
	loader.RegisterFunction(json_to_xml_function);
	
	// Register HTML extraction functions following markdown extension patterns
	
	// Define return types for HTML functions
	auto html_link_struct_type = LogicalType::STRUCT({
		{"text", LogicalType(LogicalTypeId::VARCHAR)},
		{"href", LogicalType(LogicalTypeId::VARCHAR)},
		{"title", LogicalType(LogicalTypeId::VARCHAR)},
		{"line_number", LogicalType(LogicalTypeId::BIGINT)}
	});
	
	auto html_image_struct_type = LogicalType::STRUCT({
		{"alt", LogicalType(LogicalTypeId::VARCHAR)},
		{"src", LogicalType(LogicalTypeId::VARCHAR)},
		{"title", LogicalType(LogicalTypeId::VARCHAR)},
		{"width", LogicalType(LogicalTypeId::BIGINT)},
		{"height", LogicalType(LogicalTypeId::BIGINT)},
		{"line_number", LogicalType(LogicalTypeId::BIGINT)}
	});
	
	auto html_table_row_struct_type = LogicalType::STRUCT({
		{"table_index", LogicalType(LogicalTypeId::BIGINT)},
		{"row_type", LogicalType(LogicalTypeId::VARCHAR)},
		{"row_index", LogicalType(LogicalTypeId::BIGINT)},
		{"column_index", LogicalType(LogicalTypeId::BIGINT)},
		{"cell_value", LogicalType(LogicalTypeId::VARCHAR)},
		{"line_number", LogicalType(LogicalTypeId::BIGINT)},
		{"num_columns", LogicalType(LogicalTypeId::BIGINT)},
		{"num_rows", LogicalType(LogicalTypeId::BIGINT)}
	});
	
	auto html_table_json_struct_type = LogicalType::STRUCT({
		{"table_index", LogicalType(LogicalTypeId::BIGINT)},
		{"line_number", LogicalType(LogicalTypeId::BIGINT)},
		{"num_columns", LogicalType(LogicalTypeId::BIGINT)},
		{"num_rows", LogicalType(LogicalTypeId::BIGINT)},
		{"headers", LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))},
		{"table_data", LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)))},
		{"table_json", LogicalType::STRUCT({})}, // Complex nested struct
		{"json_structure", LogicalType::STRUCT({})} // Complex nested struct
	});
	
	// Register html_extract_text function with XPath support
	ScalarFunctionSet html_extract_text_functions("html_extract_text");
	html_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType()}, LogicalType::VARCHAR, HTMLExtractTextFunction));
	html_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType::VARCHAR}, LogicalType::VARCHAR, HTMLExtractTextWithXPathFunction));
	html_extract_text_functions.AddFunction(ScalarFunction({XMLTypes::HTMLType(), LogicalType(LogicalTypeId::STRING_LITERAL)}, LogicalType::VARCHAR, HTMLExtractTextWithXPathFunction));
	loader.RegisterFunction(html_extract_text_functions);
	
	// Register html_extract_links function
	auto html_extract_links_function = ScalarFunction("html_extract_links", 
		{XMLTypes::HTMLType()}, LogicalType::LIST(html_link_struct_type), HTMLExtractLinksFunction);
	loader.RegisterFunction(html_extract_links_function);
	
	// Register html_extract_images function
	auto html_extract_images_function = ScalarFunction("html_extract_images", 
		{XMLTypes::HTMLType()}, LogicalType::LIST(html_image_struct_type), HTMLExtractImagesFunction);
	loader.RegisterFunction(html_extract_images_function);
	
	// Register html_extract_table_rows function
	auto html_extract_table_rows_function = ScalarFunction("html_extract_table_rows", 
		{XMLTypes::HTMLType()}, LogicalType::LIST(html_table_row_struct_type), HTMLExtractTableRowsFunction);
	loader.RegisterFunction(html_extract_table_rows_function);
	
	// Register html_extract_tables_json function
	auto html_extract_tables_json_function = ScalarFunction("html_extract_tables_json", 
		{XMLTypes::HTMLType()}, LogicalType::LIST(html_table_json_struct_type), HTMLExtractTablesJSONFunction);
	loader.RegisterFunction(html_extract_tables_json_function);
	
	// Register parse_html scalar function for parsing HTML content directly
	auto parse_html_function = ScalarFunction("parse_html", 
		{LogicalType::VARCHAR}, XMLTypes::HTMLType(), ReadHTMLFunction);
	loader.RegisterFunction(parse_html_function);
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
		html_vector, xpath_vector, result, args.size(),
		[&](string_t html_str, string_t xpath_str) {
			std::string html_string = html_str.GetString();
			std::string xpath_string = xpath_str.GetString();
			std::string extracted_text = XMLUtils::ExtractHTMLTextByXPath(html_string, xpath_string);
			return StringVector::AddString(result, extracted_text);
		});
}

void XMLScalarFunctions::HTMLExtractLinksFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto html_str = FlatVector::GetData<string_t>(html_vector)[i];
		std::string html_string = html_str.GetString();
		
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
		
		auto link_struct_type = LogicalType::STRUCT({
			make_pair("text", LogicalType::VARCHAR),
			make_pair("href", LogicalType::VARCHAR),
			make_pair("title", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(link_struct_type, link_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::HTMLExtractImagesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto html_str = FlatVector::GetData<string_t>(html_vector)[i];
		std::string html_string = html_str.GetString();
		
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
		
		auto image_struct_type = LogicalType::STRUCT({
			make_pair("alt", LogicalType::VARCHAR),
			make_pair("src", LogicalType::VARCHAR),
			make_pair("title", LogicalType::VARCHAR),
			make_pair("width", LogicalType::BIGINT),
			make_pair("height", LogicalType::BIGINT),
			make_pair("line_number", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(image_struct_type, image_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::HTMLExtractTableRowsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto html_str = FlatVector::GetData<string_t>(html_vector)[i];
		std::string html_string = html_str.GetString();
		
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
		
		auto table_row_struct_type = LogicalType::STRUCT({
			make_pair("table_index", LogicalType::BIGINT),
			make_pair("row_type", LogicalType::VARCHAR),
			make_pair("row_index", LogicalType::BIGINT),
			make_pair("column_index", LogicalType::BIGINT),
			make_pair("cell_value", LogicalType::VARCHAR),
			make_pair("line_number", LogicalType::BIGINT),
			make_pair("num_columns", LogicalType::BIGINT),
			make_pair("num_rows", LogicalType::BIGINT)
		});
		
		Value list_value = Value::LIST(table_row_struct_type, row_values);
		result.SetValue(i, list_value);
	}
}

void XMLScalarFunctions::HTMLExtractTablesJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &html_vector = args.data[0];
	auto count = args.size();
	
	for (idx_t i = 0; i < count; i++) {
		auto html_str = FlatVector::GetData<string_t>(html_vector)[i];
		std::string html_string = html_str.GetString();
		
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
					row_obj.push_back({headers[j], Value(row[j])});
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
			make_pair("table_index", LogicalType::BIGINT),
			make_pair("line_number", LogicalType::BIGINT),
			make_pair("num_columns", LogicalType::BIGINT),
			make_pair("num_rows", LogicalType::BIGINT),
			make_pair("headers", LogicalType::LIST(LogicalType::VARCHAR)),
			make_pair("table_data", LogicalType::LIST(LogicalType::LIST(LogicalType::VARCHAR))),
			make_pair("table_json", LogicalType::STRUCT({})), // Complex nested struct
			make_pair("json_structure", LogicalType::STRUCT({})) // Complex nested struct
		});
		
		Value list_value = Value::LIST(table_json_struct_type, table_values);
		result.SetValue(i, list_value);
	}
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
			file_handle->Read((void*)content.data(), file_size);
			
			// Parse the HTML using the HTML parser to normalize it (removes DOCTYPE)
			XMLDocRAII html_doc(content, true); // Use HTML parser
			if (html_doc.IsValid()) {
				// Serialize the document back to string (without DOCTYPE)
				xmlChar* html_output = nullptr;
				int output_size = 0;
				xmlDocDumpMemory(html_doc.doc, &html_output, &output_size);
				
				if (html_output) {
					XMLCharPtr html_ptr(html_output);
					std::string normalized_html = std::string(reinterpret_cast<const char*>(html_ptr.get()));
					
					// Remove XML declaration if present
					size_t xml_decl_end = normalized_html.find("?>");
					if (xml_decl_end != std::string::npos) {
						normalized_html = normalized_html.substr(xml_decl_end + 2);
						// Remove leading whitespace/newlines
						normalized_html.erase(0, normalized_html.find_first_not_of(" \t\n\r"));
					}
					
					// Remove DOCTYPE if present
					size_t doctype_start = normalized_html.find("<!DOCTYPE");
					if (doctype_start != std::string::npos) {
						size_t doctype_end = normalized_html.find(">", doctype_start);
						if (doctype_end != std::string::npos) {
							normalized_html.erase(doctype_start, doctype_end - doctype_start + 1);
							// Remove leading whitespace/newlines after DOCTYPE removal
							normalized_html.erase(0, normalized_html.find_first_not_of(" \t\n\r"));
						}
					}
					
					// Minify HTML: remove whitespace between tags
					std::string minified_html;
					bool in_tag = false;
					bool in_content = false;
					for (size_t i = 0; i < normalized_html.length(); i++) {
						char c = normalized_html[i];
						
						if (c == '<') {
							in_tag = true;
							in_content = false;
							minified_html += c;
						} else if (c == '>') {
							in_tag = false;
							in_content = true;
							minified_html += c;
						} else if (in_tag) {
							// Inside tag: keep all characters
							minified_html += c;
						} else if (in_content) {
							// Between tags: trim whitespace but keep content
							if (!std::isspace(c)) {
								minified_html += c;
							} else if (!minified_html.empty() && minified_html.back() != '>' && 
							          i + 1 < normalized_html.length() && normalized_html[i + 1] != '<') {
								// Keep single space between words, but not between tags
								minified_html += ' ';
							}
						}
					}
					
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
	
	UnaryExecutor::Execute<string_t, string_t>(html_content_vector, result, args.size(), [&](string_t html_content_str) {
		std::string html_content = html_content_str.GetString();
		
		// Handle empty HTML content gracefully
		if (html_content.empty()) {
			return string_t("<html></html>");
		}
		
		try {
			// Parse the HTML using the HTML parser to normalize it
			XMLDocRAII html_doc(html_content, true); // Use HTML parser
			if (html_doc.IsValid()) {
				// Serialize the document back to string
				xmlChar* html_output = nullptr;
				int output_size = 0;
				xmlDocDumpMemory(html_doc.doc, &html_output, &output_size);
				
				if (html_output) {
					XMLCharPtr html_ptr(html_output);
					std::string normalized_html = std::string(reinterpret_cast<const char*>(html_ptr.get()));
					
					// Remove XML declaration if present
					size_t xml_decl_end = normalized_html.find("?>");
					if (xml_decl_end != std::string::npos) {
						normalized_html = normalized_html.substr(xml_decl_end + 2);
						// Remove leading whitespace/newlines
						normalized_html.erase(0, normalized_html.find_first_not_of(" \t\n\r"));
					}
					
					// Remove DOCTYPE if present
					size_t doctype_start = normalized_html.find("<!DOCTYPE");
					if (doctype_start != std::string::npos) {
						size_t doctype_end = normalized_html.find(">", doctype_start);
						if (doctype_end != std::string::npos) {
							normalized_html.erase(doctype_start, doctype_end - doctype_start + 1);
							// Remove leading whitespace/newlines after DOCTYPE removal
							normalized_html.erase(0, normalized_html.find_first_not_of(" \t\n\r"));
						}
					}
					
					// Minify HTML: remove whitespace between tags
					std::string minified_html;
					bool inside_tag = false;
					bool last_was_space = false;
					bool between_tags = true; // Start assuming we're between tags
					
					for (size_t i = 0; i < normalized_html.length(); i++) {
						char c = normalized_html[i];
						
						if (c == '<') {
							inside_tag = true;
							between_tags = false;
							minified_html += c;
							last_was_space = false;
						} else if (c == '>') {
							inside_tag = false;
							between_tags = true;
							minified_html += c;
							last_was_space = false;
						} else if (inside_tag) {
							minified_html += c;
							last_was_space = false;
						} else {
							if (std::isspace(c)) {
								if (between_tags) {
									// Skip all whitespace between tags
									continue;
								} else if (!last_was_space) {
									// Keep single space between words within text content
									minified_html += ' ';
								}
								last_was_space = true;
							} else {
								between_tags = false;
								minified_html += c;
								last_was_space = false;
							}
						}
					}
					
					// Trim trailing whitespace
					if (!minified_html.empty() && std::isspace(minified_html.back())) {
						minified_html.erase(minified_html.find_last_not_of(" \t\n\r") + 1);
					}
					
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

} // namespace duckdb
