#include "xml_reader_functions.hpp"
#include "xml_utils.hpp"
#include "xml_schema_inference.hpp"
#include "xml_types.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/replacement_scan.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/common/multi_file/multi_file_reader.hpp"

namespace duckdb {

// =============================================================================
// Internal Unified Functions (used by both XML and HTML)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadDocumentObjectsBind(ClientContext &context,
                                                                      TableFunctionBindInput &input,
                                                                      vector<LogicalType> &return_types,
                                                                      vector<string> &names, ParseMode mode) {
	auto result = make_uniq<XMLReadFunctionData>();
	result->parse_mode = mode;

	// Get file pattern(s) from first argument
	const char *function_name = (mode == ParseMode::HTML) ? "read_html_objects" : "read_xml_objects";
	if (input.inputs.empty()) {
		throw InvalidInputException("%s requires at least one argument (file pattern or array of file patterns)",
		                            function_name);
	}

	vector<string> file_patterns;

	// Handle both single string and array of strings
	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR) {
		// Single file pattern
		file_patterns.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		// Array of file patterns
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				throw InvalidInputException("%s cannot process NULL file patterns", function_name);
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw InvalidInputException("%s array parameter must contain only strings", function_name);
			}
			file_patterns.push_back(child.ToString());
		}
	} else {
		throw InvalidInputException("%s first argument must be a string or array of strings", function_name);
	}

	// Expand file patterns using file system
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &pattern : file_patterns) {
		auto glob_result = fs.Glob(pattern, nullptr);
		// Extract file paths from OpenFileInfo results
		for (const auto &file_info : glob_result) {
			result->files.push_back(file_info.path);
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
	}

	// Handle optional parameters
	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
		} else if (kv.first == "filename") {
			result->include_filename = kv.second.GetValue<bool>();
		}
	}

	// Set return schema based on filename parameter and mode
	if (result->include_filename) {
		return_types.push_back(LogicalType::VARCHAR); // filename
		names.push_back("filename");
	}

	// Return appropriate type based on mode
	if (mode == ParseMode::HTML) {
		return_types.push_back(XMLTypes::HTMLType());
		names.push_back("html");
	} else {
		return_types.push_back(XMLTypes::XMLType());
		names.push_back("xml");
	}

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadDocumentInit(ClientContext &context,
                                                                           TableFunctionInitInput &input) {
	auto result = make_uniq<XMLReadGlobalState>();
	auto &bind_data = input.bind_data->Cast<XMLReadFunctionData>();

	result->files = bind_data.files;
	result->file_index = 0;

	return std::move(result);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadDocumentBind(ClientContext &context, TableFunctionBindInput &input,
                                                               vector<LogicalType> &return_types, vector<string> &names,
                                                               ParseMode mode) {
	auto result = make_uniq<XMLReadFunctionData>();
	result->parse_mode = mode;

	const char *function_name = (mode == ParseMode::HTML) ? "read_html" : "read_xml";

	// Get file pattern(s) from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("%s requires at least one argument (file pattern or array of file patterns)",
		                            function_name);
	}

	vector<string> file_patterns;

	// Handle both single string and array of strings
	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR) {
		// Single file pattern
		file_patterns.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		// Array of file patterns
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				throw InvalidInputException("%s cannot process NULL file patterns", function_name);
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw InvalidInputException("%s array parameter must contain only strings", function_name);
			}
			file_patterns.push_back(child.ToString());
		}
	} else {
		throw InvalidInputException("%s first argument must be a string or array of strings", function_name);
	}

	// Expand file patterns using file system
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &pattern : file_patterns) {
		auto glob_result = fs.Glob(pattern, nullptr);
		// Extract file paths from OpenFileInfo results
		for (const auto &file_info : glob_result) {
			result->files.push_back(file_info.path);
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
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
		} else if (kv.first == "record_element") {
			// XPath or tag name for elements that should be rows
			std::string record_value = kv.second.ToString();
			// Convert simple tag names to XPath
			if (record_value.find('/') == std::string::npos) {
				record_value = "//" + record_value;
			}
			schema_options.record_element = record_value;
		} else if (kv.first == "force_list") {
			// Handle both VARCHAR and LIST(VARCHAR)
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				// Single tag name or XPath
				std::string force_value = kv.second.ToString();
				// Convert simple tag names to XPath
				if (force_value.find('/') == std::string::npos) {
					force_value = "//" + force_value;
				}
				schema_options.force_list = force_value;
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				// List of tag names - try each one
				auto &list_children = ListValue::GetChildren(kv.second);
				std::vector<std::string> xpaths;
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						std::string tag = child.ToString();
						// Convert simple tag names to XPath
						if (tag.find('/') == std::string::npos) {
							tag = "//" + tag;
						}
						xpaths.push_back(tag);
					}
				}
				// Combine with XPath OR operator
				if (!xpaths.empty()) {
					schema_options.force_list = xpaths[0];
					for (size_t i = 1; i < xpaths.size(); i++) {
						schema_options.force_list += " | " + xpaths[i];
					}
				}
			}
		} else if (kv.first == "attr_mode") {
			schema_options.attr_mode = kv.second.ToString();
		} else if (kv.first == "attr_prefix") {
			schema_options.attr_prefix = kv.second.ToString();
		} else if (kv.first == "text_key") {
			schema_options.text_key = kv.second.ToString();
		} else if (kv.first == "namespaces") {
			schema_options.namespaces = kv.second.ToString();
		} else if (kv.first == "empty_elements") {
			schema_options.empty_elements = kv.second.ToString();
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "unnest_as") {
			auto unnest_mode = kv.second.ToString();
			if (unnest_mode == "columns") {
				schema_options.unnest_as_columns = true;
			} else if (unnest_mode == "struct") {
				schema_options.unnest_as_columns = false; // Future: implement struct mode
			} else {
				throw BinderException("%s \"unnest_as\" parameter must be 'columns' or 'struct', got: '%s'",
				                      function_name, unnest_mode);
			}
		} else if (kv.first == "columns") {
			// Handle explicit column schema specification (like JSON extension)
			auto &child_type = kv.second.type();
			if (child_type.id() != LogicalTypeId::STRUCT) {
				throw BinderException("%s \"columns\" parameter requires a struct as input.", function_name);
			}
			auto &struct_children = StructValue::GetChildren(kv.second);
			D_ASSERT(StructType::GetChildCount(child_type) == struct_children.size());

			for (idx_t i = 0; i < struct_children.size(); i++) {
				auto &name = StructType::GetChildName(child_type, i);
				auto &val = struct_children[i];
				if (val.IsNull()) {
					throw BinderException("%s \"columns\" parameter type specification cannot be NULL.", function_name);
				}
				if (val.type().id() != LogicalTypeId::VARCHAR) {
					throw BinderException("%s \"columns\" parameter type specification must be VARCHAR.",
					                      function_name);
				}

				// Parse the type string using DuckDB's type parser
				auto logical_type = TransformStringToLogicalType(StringValue::Get(val), context);

				return_types.push_back(logical_type);
				names.push_back(name);
			}

			if (return_types.empty()) {
				throw BinderException("%s \"columns\" parameter needs at least one column.", function_name);
			}

			// Store explicit schema in function data
			result->has_explicit_schema = true;
			result->column_names = names;
			result->column_types = return_types;

			has_explicit_columns = true;
		}
	}

	// Store schema options in bind_data for use during execution
	result->schema_options = schema_options;

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			auto first_file = result->files[0];
			auto file_handle = fs.OpenFile(first_file, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			if (file_size > result->max_file_size) {
				if (!result->ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", first_file,
					                            result->max_file_size);
				}
				// Fallback to simple schema based on mode
				if (mode == ParseMode::HTML) {
					return_types.push_back(XMLTypes::HTMLType());
					names.push_back("html");
				} else {
					return_types.push_back(XMLTypes::XMLType());
					names.push_back("xml");
				}
				return std::move(result);
			}

			// Read file content for schema inference
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

			// For XML mode, validate; for HTML mode, be more lenient
			if (mode == ParseMode::XML) {
				if (!XMLUtils::IsValidXML(content)) {
					if (!result->ignore_errors) {
						throw InvalidInputException("File %s contains invalid XML", first_file);
					}
					// Fallback to simple schema
					return_types.push_back(XMLTypes::XMLType());
					names.push_back("xml");
					return std::move(result);
				}
			}
			// HTML mode: skip validation, let libxml2's HTML parser handle malformed content

			// Perform schema inference (works for both XML and HTML since both produce xmlDoc)
			auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);

			// Convert to DuckDB schema
			for (const auto &col_info : inferred_columns) {
				return_types.push_back(col_info.type);
				names.push_back(col_info.name);
			}

		} catch (const Exception &e) {
			if (!result->ignore_errors) {
				throw;
			}
			// Fallback to simple schema on any error based on mode
			if (mode == ParseMode::HTML) {
				return_types.push_back(XMLTypes::HTMLType());
				names.push_back("html");
			} else {
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
			}
		}

		// Ensure we have at least one column if inference failed
		if (return_types.empty()) {
			if (mode == ParseMode::HTML) {
				return_types.push_back(XMLTypes::HTMLType());
				names.push_back("html");
			} else {
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
			}
		}
	}

	return std::move(result);
}

void XMLReaderFunctions::ReadDocumentObjectsFunction(ClientContext &context, TableFunctionInput &data_p,
                                                      DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();

	if (gstate.file_index >= gstate.files.size()) {
		// No more files to process
		return;
	}

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	const bool is_html = (bind_data.parse_mode == ParseMode::HTML);

	while (output_idx < STANDARD_VECTOR_SIZE && gstate.file_index < gstate.files.size()) {
		const auto &filename = gstate.files[gstate.file_index++];

		try {
			// Check file size
			auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			if (file_size > bind_data.max_file_size) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				continue; // Skip this file
			}

			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

			// Validate content based on mode
			if (is_html) {
				// HTML mode: be lenient, allow empty files
				if (content.empty()) {
					if (bind_data.ignore_errors) {
						continue; // Skip empty file
					} else {
						content = "<html></html>"; // Minimal valid HTML
					}
				}
			} else {
				// XML mode: strict validation
				bool is_valid = XMLUtils::IsValidXML(content);
				if (!is_valid) {
					if (bind_data.ignore_errors) {
						continue; // Skip this invalid file
					} else {
						throw InvalidInputException("File %s contains invalid XML", filename);
					}
				}
			}

			// Set output values based on schema
			idx_t col_idx = 0;
			if (bind_data.include_filename) {
				output.data[col_idx++].SetValue(output_idx, Value(filename));
			}
			output.data[col_idx++].SetValue(output_idx, Value(content));
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

void XMLReaderFunctions::ReadDocumentFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();

	if (gstate.file_index >= gstate.files.size()) {
		// No more files to process
		return;
	}

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	const bool is_html = (bind_data.parse_mode == ParseMode::HTML);

	// Get schema inference options from bind_data
	const auto &schema_options = bind_data.schema_options;

	while (output_idx < STANDARD_VECTOR_SIZE && gstate.file_index < gstate.files.size()) {
		const auto &filename = gstate.files[gstate.file_index++];

		try {
			// Check file size
			auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			if (file_size > bind_data.max_file_size) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				continue; // Skip this file
			}

			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

			// Validate content based on mode
			if (is_html) {
				// HTML mode: be lenient, skip validation
				// Let libxml2's HTML parser handle malformed content
			} else {
				// XML mode: strict validation
				if (!XMLUtils::IsValidXML(content)) {
					if (bind_data.ignore_errors) {
						continue; // Skip this invalid file
					} else {
						throw InvalidInputException("File %s contains invalid XML", filename);
					}
				}
			}

			// Extract structured data using appropriate method
			std::vector<std::vector<Value>> extracted_rows;

			if (bind_data.has_explicit_schema) {
				// Use explicit schema for extraction
				extracted_rows = XMLSchemaInference::ExtractDataWithSchema(content, bind_data.column_names,
				                                                           bind_data.column_types, schema_options);
			} else {
				// Use schema inference
				extracted_rows = XMLSchemaInference::ExtractData(content, schema_options);
			}

			// Fill output vectors with extracted data
			for (const auto &row : extracted_rows) {
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

// =============================================================================
// Public XML Functions (will be refactored to delegate in Phase 3)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                vector<LogicalType> &return_types,
                                                                vector<string> &names) {
	auto result = make_uniq<XMLReadFunctionData>();

	// Get file pattern(s) from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException(
		    "read_xml_objects requires at least one argument (file pattern or array of file patterns)");
	}

	vector<string> file_patterns;

	// Handle both single string and array of strings
	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR) {
		// Single file pattern
		file_patterns.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		// Array of file patterns
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				throw InvalidInputException("read_xml_objects cannot process NULL file patterns");
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw InvalidInputException("read_xml_objects array parameter must contain only strings");
			}
			file_patterns.push_back(child.ToString());
		}
	} else {
		throw InvalidInputException("read_xml_objects first argument must be a string or array of strings");
	}

	// Expand file patterns using file system
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &pattern : file_patterns) {
		auto glob_result = fs.Glob(pattern, nullptr);
		// Extract file paths from OpenFileInfo results
		for (const auto &file_info : glob_result) {
			result->files.push_back(file_info.path);
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
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

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadXMLObjectsInit(ClientContext &context,
                                                                            TableFunctionInitInput &input) {
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
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				continue; // Skip this file
			}

			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

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

	// Get file pattern(s) from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("read_xml requires at least one argument (file pattern or array of file patterns)");
	}

	vector<string> file_patterns;

	// Handle both single string and array of strings
	const auto &first_input = input.inputs[0];
	if (first_input.type().id() == LogicalTypeId::VARCHAR) {
		// Single file pattern
		file_patterns.push_back(first_input.ToString());
	} else if (first_input.type().id() == LogicalTypeId::LIST) {
		// Array of file patterns
		auto &list_children = ListValue::GetChildren(first_input);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				throw InvalidInputException("read_xml cannot process NULL file patterns");
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw InvalidInputException("read_xml array parameter must contain only strings");
			}
			file_patterns.push_back(child.ToString());
		}
	} else {
		throw InvalidInputException("read_xml first argument must be a string or array of strings");
	}

	// Expand file patterns using file system
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &pattern : file_patterns) {
		auto glob_result = fs.Glob(pattern, nullptr);
		// Extract file paths from OpenFileInfo results
		for (const auto &file_info : glob_result) {
			result->files.push_back(file_info.path);
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
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
		} else if (kv.first == "record_element") {
			// XPath or tag name for elements that should be rows
			std::string record_value = kv.second.ToString();
			// Convert simple tag names to XPath
			if (record_value.find('/') == std::string::npos) {
				record_value = "//" + record_value;
			}
			schema_options.record_element = record_value;
		} else if (kv.first == "force_list") {
			// Handle both VARCHAR and LIST(VARCHAR)
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				// Single tag name or XPath
				std::string force_value = kv.second.ToString();
				// Convert simple tag names to XPath
				if (force_value.find('/') == std::string::npos) {
					force_value = "//" + force_value;
				}
				schema_options.force_list = force_value;
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				// List of tag names - try each one
				auto &list_children = ListValue::GetChildren(kv.second);
				std::vector<std::string> xpaths;
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						std::string tag = child.ToString();
						// Convert simple tag names to XPath
						if (tag.find('/') == std::string::npos) {
							tag = "//" + tag;
						}
						xpaths.push_back(tag);
					}
				}
				// Combine with XPath OR operator
				if (!xpaths.empty()) {
					schema_options.force_list = xpaths[0];
					for (size_t i = 1; i < xpaths.size(); i++) {
						schema_options.force_list += " | " + xpaths[i];
					}
				}
			}
		} else if (kv.first == "attr_mode") {
			schema_options.attr_mode = kv.second.ToString();
		} else if (kv.first == "attr_prefix") {
			schema_options.attr_prefix = kv.second.ToString();
		} else if (kv.first == "text_key") {
			schema_options.text_key = kv.second.ToString();
		} else if (kv.first == "namespaces") {
			schema_options.namespaces = kv.second.ToString();
		} else if (kv.first == "empty_elements") {
			schema_options.empty_elements = kv.second.ToString();
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "unnest_as") {
			auto unnest_mode = kv.second.ToString();
			if (unnest_mode == "columns") {
				schema_options.unnest_as_columns = true;
			} else if (unnest_mode == "struct") {
				schema_options.unnest_as_columns = false; // Future: implement struct mode
			} else {
				throw BinderException("read_xml \"unnest_as\" parameter must be 'columns' or 'struct', got: '%s'",
				                      unnest_mode);
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

	// Store schema options in bind_data for use during execution
	result->schema_options = schema_options;

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			auto first_file = result->files[0];
			auto file_handle = fs.OpenFile(first_file, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			if (file_size > result->max_file_size) {
				if (!result->ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", first_file,
					                            result->max_file_size);
				}
				// Fallback to simple schema
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
				return std::move(result);
			}

			// Read file content for schema inference
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

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
			for (const auto &col_info : inferred_columns) {
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

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadXMLInit(ClientContext &context,
                                                                     TableFunctionInitInput &input) {
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

	// Get schema inference options from bind_data
	const auto &schema_options = bind_data.schema_options;

	while (output_idx < STANDARD_VECTOR_SIZE && gstate.file_index < gstate.files.size()) {
		const auto &filename = gstate.files[gstate.file_index++];

		try {
			// Check file size
			auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
			auto file_size = fs.GetFileSize(*file_handle);

			if (file_size > bind_data.max_file_size) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				continue; // Skip this file
			}

			// Read file content
			string content;
			content.resize(file_size);
			file_handle->Read((void *)content.data(), file_size);

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
				extracted_rows = XMLSchemaInference::ExtractDataWithSchema(content, bind_data.column_names,
				                                                           bind_data.column_types, schema_options);
			} else {
				// Use schema inference
				extracted_rows = XMLSchemaInference::ExtractData(content, schema_options);
			}

			// Fill output vectors with extracted data
			for (const auto &row : extracted_rows) {
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

// =============================================================================
// Public HTML Functions (delegate to internal unified functions)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                  vector<LogicalType> &return_types,
                                                                  vector<string> &names) {
	return ReadDocumentObjectsBind(context, input, return_types, names, ParseMode::HTML);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadHTMLObjectsInit(ClientContext &context,
                                                                               TableFunctionInitInput &input) {
	return ReadDocumentInit(context, input);
}

void XMLReaderFunctions::ReadHTMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p,
                                                  DataChunk &output) {
	return ReadDocumentObjectsFunction(context, data_p, output);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadHTMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types, vector<string> &names) {
	return ReadDocumentBind(context, input, return_types, names, ParseMode::HTML);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadHTMLInit(ClientContext &context,
                                                                       TableFunctionInitInput &input) {
	return ReadDocumentInit(context, input);
}

void XMLReaderFunctions::ReadHTMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	return ReadDocumentFunction(context, data_p, output);
}

void XMLReaderFunctions::Register(ExtensionLoader &loader) {
	// Register read_xml_objects table function (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_xml_objects_set("read_xml_objects");

	// Variant 1: Single string parameter
	TableFunction read_xml_objects_single("read_xml_objects", {LogicalType::VARCHAR}, ReadXMLObjectsFunction,
	                                      ReadXMLObjectsBind, ReadXMLObjectsInit);
	read_xml_objects_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_objects_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_xml_objects_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_xml_objects_set.AddFunction(read_xml_objects_single);

	// Variant 2: Array of strings parameter
	TableFunction read_xml_objects_array("read_xml_objects", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                     ReadXMLObjectsFunction, ReadXMLObjectsBind, ReadXMLObjectsInit);
	read_xml_objects_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_objects_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_xml_objects_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_xml_objects_set.AddFunction(read_xml_objects_array);

	loader.RegisterFunction(read_xml_objects_set);

	// Register read_xml table function with schema inference (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_xml_set("read_xml");

	// Variant 1: Single string parameter
	TableFunction read_xml_single("read_xml", {LogicalType::VARCHAR}, ReadXMLFunction, ReadXMLBind, ReadXMLInit);
	read_xml_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	// Schema inference parameters
	read_xml_single.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_xml_single.named_parameters["attr_mode"] = LogicalType::VARCHAR;       // 'columns' | 'prefixed' | 'map' | 'discard'
	read_xml_single.named_parameters["attr_prefix"] = LogicalType::VARCHAR;     // Prefix for attributes when attr_mode='prefixed'
	read_xml_single.named_parameters["text_key"] = LogicalType::VARCHAR;        // Key for mixed text content
	read_xml_single.named_parameters["namespaces"] = LogicalType::VARCHAR;      // 'strip' | 'expand' | 'keep'
	read_xml_single.named_parameters["empty_elements"] = LogicalType::VARCHAR;  // 'null' | 'string' | 'object'
	read_xml_single.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_xml_single.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_xml_single.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_xml_single.named_parameters["record_element"] = LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_xml_single.named_parameters["force_list"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_xml_single.named_parameters["columns"] = LogicalType::ANY;
	read_xml_set.AddFunction(read_xml_single);

	// Variant 2: Array of strings parameter
	TableFunction read_xml_array("read_xml", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadXMLFunction, ReadXMLBind,
	                             ReadXMLInit);
	read_xml_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	// Schema inference parameters
	read_xml_array.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_xml_array.named_parameters["attr_mode"] = LogicalType::VARCHAR;       // 'columns' | 'prefixed' | 'map' | 'discard'
	read_xml_array.named_parameters["attr_prefix"] = LogicalType::VARCHAR;     // Prefix for attributes when attr_mode='prefixed'
	read_xml_array.named_parameters["text_key"] = LogicalType::VARCHAR;        // Key for mixed text content
	read_xml_array.named_parameters["namespaces"] = LogicalType::VARCHAR;      // 'strip' | 'expand' | 'keep'
	read_xml_array.named_parameters["empty_elements"] = LogicalType::VARCHAR;  // 'null' | 'string' | 'object'
	read_xml_array.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_xml_array.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_xml_array.named_parameters["record_element"] = LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_xml_array.named_parameters["force_list"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_xml_array.named_parameters["columns"] = LogicalType::ANY;
	read_xml_set.AddFunction(read_xml_array);

	loader.RegisterFunction(read_xml_set);

	// Register read_html table function for reading HTML files (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_html_set("read_html");

	// Variant 1: Single string parameter
	TableFunction read_html_single("read_html", {LogicalType::VARCHAR}, ReadHTMLFunction, ReadHTMLBind, ReadHTMLInit);
	read_html_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_set.AddFunction(read_html_single);

	// Variant 2: Array of strings parameter
	TableFunction read_html_array("read_html", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadHTMLFunction,
	                              ReadHTMLBind, ReadHTMLInit);
	read_html_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_set.AddFunction(read_html_array);

	loader.RegisterFunction(read_html_set);

	// Register read_html_objects table function for batch HTML processing (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_html_objects_set("read_html_objects");

	// Variant 1: Single string parameter
	TableFunction read_html_objects_single("read_html_objects", {LogicalType::VARCHAR}, ReadHTMLObjectsFunction,
	                                       ReadHTMLObjectsBind, ReadHTMLObjectsInit);
	read_html_objects_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_objects_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_objects_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_objects_set.AddFunction(read_html_objects_single);

	// Variant 2: Array of strings parameter
	TableFunction read_html_objects_array("read_html_objects", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                      ReadHTMLObjectsFunction, ReadHTMLObjectsBind, ReadHTMLObjectsInit);
	read_html_objects_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_objects_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_objects_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	read_html_objects_set.AddFunction(read_html_objects_array);

	loader.RegisterFunction(read_html_objects_set);

	// Register html_extract_tables table function
	TableFunction html_extract_tables_function("html_extract_tables", {LogicalType::VARCHAR}, HTMLExtractTablesFunction,
	                                           HTMLExtractTablesBind, HTMLExtractTablesInit);
	loader.RegisterFunction(html_extract_tables_function);
}


unique_ptr<FunctionData> XMLReaderFunctions::HTMLExtractTablesBind(ClientContext &context,
                                                                   TableFunctionBindInput &input,
                                                                   vector<LogicalType> &return_types,
                                                                   vector<string> &names) {
	auto result = make_uniq<HTMLTableExtractionData>();

	// Get HTML content from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("html_extract_tables requires HTML content as first argument");
	}

	result->html_content = input.inputs[0].ToString();

	// Set return schema: table_index, row_index, columns
	return_types.push_back(LogicalType::BIGINT); // table_index
	names.push_back("table_index");
	return_types.push_back(LogicalType::BIGINT); // row_index
	names.push_back("row_index");
	return_types.push_back(LogicalType::LIST(LogicalType::VARCHAR)); // columns
	names.push_back("columns");

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::HTMLExtractTablesInit(ClientContext &context,
                                                                               TableFunctionInitInput &input) {
	auto result = make_uniq<HTMLTableExtractionGlobalState>();
	auto &bind_data = input.bind_data->Cast<HTMLTableExtractionData>();

	// Extract tables from the HTML content
	auto tables = XMLUtils::ExtractHTMLTables(bind_data.html_content);

	// Convert to our format: [table][row][column]
	for (const auto &table : tables) {
		vector<vector<string>> table_rows;

		// Add header row if present
		if (!table.headers.empty()) {
			vector<string> header_row;
			for (const auto &header : table.headers) {
				header_row.push_back(header);
			}
			table_rows.push_back(header_row);
		}

		// Add data rows
		for (const auto &row : table.rows) {
			vector<string> data_row;
			for (const auto &cell : row) {
				data_row.push_back(cell);
			}
			table_rows.push_back(data_row);
		}

		result->all_tables.push_back(table_rows);
	}

	return std::move(result);
}

void XMLReaderFunctions::HTMLExtractTablesFunction(ClientContext &context, TableFunctionInput &data_p,
                                                   DataChunk &output) {
	auto &gstate = data_p.global_state->Cast<HTMLTableExtractionGlobalState>();

	idx_t output_idx = 0;

	while (output_idx < STANDARD_VECTOR_SIZE && gstate.current_table < gstate.all_tables.size()) {
		const auto &current_table = gstate.all_tables[gstate.current_table];

		if (gstate.current_row < current_table.size()) {
			const auto &current_row = current_table[gstate.current_row];

			// Set table_index
			output.data[0].SetValue(output_idx, Value::BIGINT(static_cast<int64_t>(gstate.current_table)));

			// Set row_index
			output.data[1].SetValue(output_idx, Value::BIGINT(static_cast<int64_t>(gstate.current_row)));

			// Set columns as list of strings
			vector<Value> column_values;
			for (const auto &column : current_row) {
				column_values.emplace_back(Value(column));
			}
			Value columns_list = Value::LIST(LogicalType::VARCHAR, column_values);
			output.data[2].SetValue(output_idx, columns_list);

			output_idx++;
			gstate.current_row++;
		} else {
			// Move to next table
			gstate.current_table++;
			gstate.current_row = 0;
		}
	}

	output.SetCardinality(output_idx);
}

} // namespace duckdb
