#include "xml_reader_functions.hpp"
#include "xml_utils.hpp"
#include "xml_schema_inference.hpp"
#include "xml_types.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/replacement_scan.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"

namespace duckdb {

unique_ptr<FunctionData> XMLReaderFunctions::ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                  vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<XMLReadFunctionData>();
	
	// Get file pattern from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("read_xml_objects requires at least one argument (file pattern)");
	}
	
	auto file_pattern = input.inputs[0].ToString();
	
	// Expand file pattern using file system
	auto &fs = FileSystem::GetFileSystem(context);
	auto glob_result = fs.Glob(file_pattern, nullptr);
	
	// Extract file paths from OpenFileInfo results
	for (const auto &file_info : glob_result) {
		result->files.push_back(file_info.path);
	}
	
	if (result->files.empty()) {
		throw InvalidInputException("No files found matching pattern: %s", file_pattern);
	}
	
	// Handle optional parameters
	bool include_filename = false; // Default: don't include filename column
	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
		} else if (kv.first == "filename") {
			include_filename = kv.second.GetValue<bool>();
		}
	}
	
	// Set return schema based on filename parameter
	if (include_filename) {
		return_types.push_back(LogicalType::VARCHAR); // filename
		names.push_back("filename");
	}
	return_types.push_back(XMLTypes::XMLType()); // XML content
	names.push_back("xml");
	
	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadXMLObjectsInit(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<XMLReadGlobalState>();
	auto &bind_data = input.bind_data->Cast<XMLReadFunctionData>();
	
	result->files = bind_data.files;
	result->file_index = 0;
	
	return std::move(result);
}

void XMLReaderFunctions::ReadXMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();
	
	if (gstate.file_index >= gstate.files.size()) {
		// No more files to process
		return;
	}
	
	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	
	while (output_idx < STANDARD_VECTOR_SIZE && gstate.file_index < gstate.files.size()) {
		const auto &filename = gstate.files[gstate.file_index++];
		
		try {
			// Check file size
			auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);
			
			if (file_size > bind_data.max_file_size) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", 
					                           filename, bind_data.max_file_size);
				}
				continue; // Skip this file
			}
			
			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void*)content.data(), file_size);
			
			// Validate XML 
			bool is_valid = XMLUtils::IsValidXML(content);
			if (!is_valid) {
				if (bind_data.ignore_errors) {
					continue; // Skip this invalid file
				} else {
					throw InvalidInputException("File %s contains invalid XML", filename);
				}
			}
			
			// Set output values based on schema
			idx_t col_idx = 0;
			if (output.data.size() == 2) {
				// Both filename and xml columns
				output.data[col_idx++].SetValue(output_idx, Value(filename));
				output.data[col_idx++].SetValue(output_idx, Value(content));
			} else {
				// Only xml column
				output.data[col_idx++].SetValue(output_idx, Value(content));
			}
			output_idx++;
			
		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue
		}
	}
	
	output.SetCardinality(output_idx);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadXMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<XMLReadFunctionData>();
	
	// Get file pattern from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("read_xml requires at least one argument (file pattern)");
	}
	
	auto file_pattern = input.inputs[0].ToString();
	
	// Expand file pattern using file system
	auto &fs = FileSystem::GetFileSystem(context);
	auto glob_result = fs.Glob(file_pattern, nullptr);
	
	// Extract file paths from OpenFileInfo results
	for (const auto &file_info : glob_result) {
		result->files.push_back(file_info.path);
	}
	
	if (result->files.empty()) {
		throw InvalidInputException("No files found matching pattern: %s", file_pattern);
	}
	
	// Handle optional parameters with schema inference defaults
	XMLSchemaOptions schema_options;
	bool has_explicit_columns = false;
	
	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
			schema_options.ignore_errors = result->ignore_errors;
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
			schema_options.maximum_file_size = result->max_file_size;
		} else if (kv.first == "root_element") {
			schema_options.root_element = kv.second.ToString();
		} else if (kv.first == "include_attributes") {
			schema_options.include_attributes = kv.second.GetValue<bool>();
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "unnest_as") {
			auto unnest_mode = kv.second.ToString();
			if (unnest_mode == "columns") {
				schema_options.unnest_as_columns = true;
			} else if (unnest_mode == "struct") {
				schema_options.unnest_as_columns = false;  // Future: implement struct mode
			} else {
				throw BinderException("read_xml \"unnest_as\" parameter must be 'columns' or 'struct', got: '%s'", unnest_mode);
			}
		} else if (kv.first == "columns") {
			// Handle explicit column schema specification (like JSON extension)
			auto &child_type = kv.second.type();
			if (child_type.id() != LogicalTypeId::STRUCT) {
				throw BinderException("read_xml \"columns\" parameter requires a struct as input.");
			}
			auto &struct_children = StructValue::GetChildren(kv.second);
			D_ASSERT(StructType::GetChildCount(child_type) == struct_children.size());
			
			for (idx_t i = 0; i < struct_children.size(); i++) {
				auto &name = StructType::GetChildName(child_type, i);
				auto &val = struct_children[i];
				if (val.IsNull()) {
					throw BinderException("read_xml \"columns\" parameter type specification cannot be NULL.");
				}
				if (val.type().id() != LogicalTypeId::VARCHAR) {
					throw BinderException("read_xml \"columns\" parameter type specification must be VARCHAR.");
				}
				
				// Parse the type string using DuckDB's type parser
				auto logical_type = TransformStringToLogicalType(StringValue::Get(val), context);
				
				return_types.push_back(logical_type);
				names.push_back(name);
			}
			
			if (return_types.empty()) {
				throw BinderException("read_xml \"columns\" parameter needs at least one column.");
			}
			
			// Store explicit schema in function data
			result->has_explicit_schema = true;
			result->column_names = names;
			result->column_types = return_types;
			
			has_explicit_columns = true;
		}
	}
	
	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			auto first_file = result->files[0];
			auto file_handle = fs.OpenFile(first_file, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);
			
			if (file_size > result->max_file_size) {
				if (!result->ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", 
					                           first_file, result->max_file_size);
				}
				// Fallback to simple schema
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
				return std::move(result);
			}
			
			// Read file content for schema inference
			string content;
			content.resize(file_size);
			file_handle->Read((void*)content.data(), file_size);
			
			// Validate XML
			if (!XMLUtils::IsValidXML(content)) {
				if (!result->ignore_errors) {
					throw InvalidInputException("File %s contains invalid XML", first_file);
				}
				// Fallback to simple schema
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
				return std::move(result);
			}
			
			// Perform schema inference
			auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);
			
			// Convert to DuckDB schema
			for (const auto& col_info : inferred_columns) {
				return_types.push_back(col_info.type);
				names.push_back(col_info.name);
			}
			
		} catch (const Exception &e) {
			if (!result->ignore_errors) {
				throw;
			}
			// Fallback to simple schema on any error
			return_types.push_back(XMLTypes::XMLType());
			names.push_back("xml");
		}
		
		// Ensure we have at least one column if inference failed
		if (return_types.empty()) {
			return_types.push_back(XMLTypes::XMLType());
			names.push_back("xml");
		}
	}
	
	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadXMLInit(ClientContext &context, TableFunctionInitInput &input) {
	return ReadXMLObjectsInit(context, input);
}

void XMLReaderFunctions::ReadXMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();
	
	if (gstate.file_index >= gstate.files.size()) {
		// No more files to process
		return;
	}
	
	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	
	// Set up schema inference options (matching what was used in bind)
	XMLSchemaOptions schema_options;
	// TODO: Get these from bind_data if we store them there
	
	while (output_idx < STANDARD_VECTOR_SIZE && gstate.file_index < gstate.files.size()) {
		const auto &filename = gstate.files[gstate.file_index++];
		
		try {
			// Check file size
			auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);
			
			if (file_size > bind_data.max_file_size) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", 
					                           filename, bind_data.max_file_size);
				}
				continue; // Skip this file
			}
			
			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void*)content.data(), file_size);
			
			// Validate XML 
			if (!XMLUtils::IsValidXML(content)) {
				if (bind_data.ignore_errors) {
					continue; // Skip this invalid file
				} else {
					throw InvalidInputException("File %s contains invalid XML", filename);
				}
			}
			
			// Extract structured data using appropriate method
			std::vector<std::vector<Value>> extracted_rows;
			
			if (bind_data.has_explicit_schema) {
				// Use explicit schema for extraction
				extracted_rows = XMLSchemaInference::ExtractDataWithSchema(
					content, bind_data.column_names, bind_data.column_types, schema_options);
			} else {
				// Use schema inference
				extracted_rows = XMLSchemaInference::ExtractData(content, schema_options);
			}
			
			// Fill output vectors with extracted data
			for (const auto& row : extracted_rows) {
				if (output_idx >= STANDARD_VECTOR_SIZE) {
					break;
				}
				
				// Set values for each column in the row
				for (idx_t col_idx = 0; col_idx < output.ColumnCount() && col_idx < row.size(); col_idx++) {
					output.data[col_idx].SetValue(output_idx, row[col_idx]);
				}
				output_idx++;
			}
			
		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue
		}
		
		// If we filled up the output, break
		if (output_idx >= STANDARD_VECTOR_SIZE) {
			break;
		}
	}
	
	output.SetCardinality(output_idx);
}

unique_ptr<TableRef> XMLReaderFunctions::ReadXMLReplacement(ClientContext &context, ReplacementScanInput &input,
                                                          optional_ptr<ReplacementScanData> data) {
	auto table_name = ReplacementScan::GetFullPath(input);
	
	// Check if this file can be handled by the XML extension
	if (!ReplacementScan::CanReplace(table_name, {"xml"})) {
		return nullptr;
	}
	
	// Create table function reference that calls read_xml
	auto table_function = make_uniq<TableFunctionRef>();
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(make_uniq<ConstantExpression>(Value(table_name)));
	table_function->function = make_uniq<FunctionExpression>("read_xml", std::move(children));
	
	// Set alias for non-glob patterns
	if (!FileSystem::HasGlob(table_name)) {
		auto &fs = FileSystem::GetFileSystem(context);
		table_function->alias = fs.ExtractBaseName(table_name);
	}
	
	return std::move(table_function);
}

void XMLReaderFunctions::Register(DatabaseInstance &db) {
	// Register read_xml_objects table function
	TableFunction read_xml_objects_function("read_xml_objects", {LogicalType::VARCHAR}, ReadXMLObjectsFunction, 
	                                          ReadXMLObjectsBind, ReadXMLObjectsInit);
	read_xml_objects_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_objects_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_xml_objects_function.named_parameters["filename"] = LogicalType::BOOLEAN;
	ExtensionUtil::RegisterFunction(db, read_xml_objects_function);
	
	// Register read_xml table function with schema inference
	TableFunction read_xml_function("read_xml", {LogicalType::VARCHAR}, ReadXMLFunction, 
	                                 ReadXMLBind, ReadXMLInit);
	read_xml_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	
	// Schema inference parameters
	read_xml_function.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_xml_function.named_parameters["include_attributes"] = LogicalType::BOOLEAN;
	read_xml_function.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_xml_function.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_xml_function.named_parameters["unnest_as"] = LogicalType::VARCHAR;  // 'columns' (default) or 'struct' (future)
	
	// Explicit schema specification (like JSON extension)
	read_xml_function.named_parameters["columns"] = LogicalType::ANY;
	
	ExtensionUtil::RegisterFunction(db, read_xml_function);
}

} // namespace duckdb