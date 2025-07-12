#include "xml_reader_functions.hpp"
#include "xml_utils.hpp"
#include "xml_schema_inference.hpp"
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
	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
		}
	}
	
	// Set return schema: filename, content
	return_types.push_back(LogicalType::VARCHAR); // filename
	return_types.push_back(LogicalType::VARCHAR); // content (XML)
	names.push_back("filename");
	names.push_back("content");
	
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
			
			// Set output values
			output.data[0].SetValue(output_idx, Value(filename));
			output.data[1].SetValue(output_idx, Value(content));
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
	
	// Handle optional parameters
	XMLSchemaOptions schema_options;
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
		} else if (kv.first == "schema_depth") {
			schema_options.schema_depth = kv.second.GetValue<int32_t>();
		}
	}
	
	// Perform schema inference on the first file
	try {
		auto first_file = result->files[0];
		auto file_handle = fs.OpenFile(first_file, FileFlags::FILE_FLAGS_READ);
		auto file_size = fs.GetFileSize(*file_handle);
		
		if (file_size > result->max_file_size) {
			if (!result->ignore_errors) {
				throw InvalidInputException("File %s exceeds maximum size limit", first_file);
			}
			// Fallback to simple schema
			return_types.push_back(LogicalType::VARCHAR); // filename
			return_types.push_back(LogicalType::VARCHAR); // content
			names.push_back("filename");
			names.push_back("content");
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
			return_types.push_back(LogicalType::VARCHAR); // filename
			return_types.push_back(LogicalType::VARCHAR); // content
			names.push_back("filename");
			names.push_back("content");
			return std::move(result);
		}
		
		// Perform schema inference
		auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);
		
		// Convert to DuckDB schema
		for (const auto& col_info : inferred_columns) {
			return_types.push_back(col_info.type);
			names.push_back(col_info.name);
		}
		
		// Store schema info in function data for later use
		// TODO: Store inferred schema in XMLReadFunctionData
		
	} catch (const Exception &e) {
		if (!result->ignore_errors) {
			throw;
		}
		// Fallback to simple schema
		return_types.push_back(LogicalType::VARCHAR); // filename
		return_types.push_back(LogicalType::VARCHAR); // content
		names.push_back("filename");
		names.push_back("content");
	}
	
	// Ensure we have at least some columns
	if (return_types.empty()) {
		return_types.push_back(LogicalType::VARCHAR); // filename
		return_types.push_back(LogicalType::VARCHAR); // content
		names.push_back("filename");
		names.push_back("content");
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
			
			// TODO: For now, just return filename and content like read_xml_objects
			// Full schema inference extraction will be implemented next
			output.data[0].SetValue(output_idx, Value(filename));
			output.data[1].SetValue(output_idx, Value(content));
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
	read_xml_function.named_parameters["schema_depth"] = LogicalType::INTEGER;
	
	ExtensionUtil::RegisterFunction(db, read_xml_function);
}

} // namespace duckdb