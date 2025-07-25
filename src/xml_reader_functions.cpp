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
	
	// Register read_html table function for reading HTML files
	TableFunction read_html_function("read_html", {LogicalType::VARCHAR}, ReadHTMLFunction, 
	                                  ReadHTMLBind, ReadHTMLInit);
	read_html_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_function.named_parameters["filename"] = LogicalType::BOOLEAN;
	ExtensionUtil::RegisterFunction(db, read_html_function);
	
	// Register read_html_objects table function for batch HTML processing
	TableFunction read_html_objects_function("read_html_objects", {LogicalType::VARCHAR}, ReadHTMLFunction, 
	                                          ReadHTMLBind, ReadHTMLInit);
	read_html_objects_function.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_objects_function.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_objects_function.named_parameters["filename"] = LogicalType::BOOLEAN;
	ExtensionUtil::RegisterFunction(db, read_html_objects_function);
	
	// Register html_extract_tables table function
	TableFunction html_extract_tables_function("html_extract_tables", {LogicalType::VARCHAR}, HTMLExtractTablesFunction,
	                                            HTMLExtractTablesBind, HTMLExtractTablesInit);
	ExtensionUtil::RegisterFunction(db, html_extract_tables_function);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadHTMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<XMLReadFunctionData>();
	
	// Get file pattern from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("read_html requires at least one argument (file pattern)");
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
	return_types.push_back(XMLTypes::HTMLType()); // HTML content
	names.push_back("html");
	
	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ReadHTMLInit(ClientContext &context, TableFunctionInitInput &input) {
	auto result = make_uniq<XMLReadGlobalState>();
	auto &bind_data = input.bind_data->Cast<XMLReadFunctionData>();
	
	result->files = bind_data.files;
	result->file_index = 0;
	
	return std::move(result);
}

void XMLReaderFunctions::ReadHTMLFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
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
			
			// Handle empty HTML files gracefully (HTML is more permissive than XML)
			if (content.empty()) {
				if (bind_data.ignore_errors) {
					continue; // Skip this empty file
				} else {
					// For HTML, treat empty file as empty HTML content
					content = "<html></html>";
				}
			}
			
			// Process HTML content same as parse_html function
			std::string processed_content = content;
			try {
				// Parse the HTML using the HTML parser to normalize it
				XMLDocRAII html_doc(content, true); // Use HTML parser
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
						
						processed_content = minified_html;
					}
				}
			} catch (const std::exception &e) {
				// Keep original content if processing fails
			}
			
			// Set output values based on schema
			idx_t col_idx = 0;
			if (output.data.size() == 2) {
				// Both filename and html columns
				output.data[col_idx++].SetValue(output_idx, Value(filename));
				output.data[col_idx++].SetValue(output_idx, Value(processed_content));
			} else {
				// Only html column
				output.data[col_idx++].SetValue(output_idx, Value(processed_content));
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

unique_ptr<FunctionData> XMLReaderFunctions::HTMLExtractTablesBind(ClientContext &context, TableFunctionBindInput &input,
                                                                     vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<HTMLTableExtractionData>();
	
	// Get HTML content from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("html_extract_tables requires HTML content as first argument");
	}
	
	result->html_content = input.inputs[0].ToString();
	
	// Set return schema: table_index, row_index, columns
	return_types.push_back(LogicalType::BIGINT);   // table_index
	names.push_back("table_index");
	return_types.push_back(LogicalType::BIGINT);   // row_index
	names.push_back("row_index");
	return_types.push_back(LogicalType::LIST(LogicalType::VARCHAR)); // columns
	names.push_back("columns");
	
	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::HTMLExtractTablesInit(ClientContext &context, TableFunctionInitInput &input) {
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

void XMLReaderFunctions::HTMLExtractTablesFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
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