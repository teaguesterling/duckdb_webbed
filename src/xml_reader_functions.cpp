#include "xml_reader_functions.hpp"
#include "xml_utils.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"

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
	// For now, implement a simple version that returns the same as read_xml_objects
	// Schema inference will be added later
	return ReadXMLObjectsBind(context, input, return_types, names);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadXMLInit(ClientContext &context, TableFunctionInitInput &input) {
	return ReadXMLObjectsInit(context, input);
}

void XMLReaderFunctions::ReadXMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	// For now, use the same implementation as read_xml_objects
	// Schema inference will be added later
	ReadXMLObjectsFunction(context, data_p, output);
}

void XMLReaderFunctions::Register(DatabaseInstance &db) {
	// Register read_xml_objects table function
	TableFunction read_xml_objects_function("read_xml_objects", {LogicalType::VARCHAR}, ReadXMLObjectsFunction, 
	                                          ReadXMLObjectsBind, ReadXMLObjectsInit);
	read_xml_objects_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_objects_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	ExtensionUtil::RegisterFunction(db, read_xml_objects_function);
	
	// Register read_xml table function (simplified version for now)
	TableFunction read_xml_function("read_xml", {LogicalType::VARCHAR}, ReadXMLFunction, 
	                                 ReadXMLBind, ReadXMLInit);
	read_xml_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	ExtensionUtil::RegisterFunction(db, read_xml_function);
}

} // namespace duckdb