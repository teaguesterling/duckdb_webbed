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
#include "duckdb/function/scalar/strftime_format.hpp"

namespace {

// Resolve a datetime_format preset name or format string to a list of format strings
std::vector<std::string> ResolveDatetimeFormat(const std::string &input) {
	if (input == "auto") {
		return {
		    "%Y-%m-%dT%H:%M:%S.%f%z",
		    "%Y-%m-%dT%H:%M:%S%z",
		    "%Y-%m-%dT%H:%M:%S.%f",
		    "%Y-%m-%dT%H:%M:%S",
		    "%Y-%m-%d %H:%M:%S.%f%z",
		    "%Y-%m-%d %H:%M:%S%z",
		    "%Y-%m-%d %H:%M:%S.%f",
		    "%Y-%m-%d %H:%M:%S",
		    "%Y-%m-%d",
		    "%m/%d/%Y",
		    "%d/%m/%Y",
		    "%Y/%m/%d",
		    "%H:%M:%S",
		    "%I:%M:%S %p",
		    "%H:%M",
		};
	}
	if (input == "none") {
		return {};
	}
	if (input == "us") {
		return {"%m/%d/%Y"};
	}
	if (input == "eu") {
		return {"%d/%m/%Y"};
	}
	if (input == "iso") {
		return {"%Y-%m-%d"};
	}
	if (input == "us_timestamp") {
		return {"%m/%d/%Y %I:%M:%S %p"};
	}
	if (input == "eu_timestamp") {
		return {"%d/%m/%Y %H:%M:%S"};
	}
	if (input == "iso_timestamp") {
		return {"%Y-%m-%dT%H:%M:%S"};
	}
	if (input == "iso_timestamptz") {
		return {"%Y-%m-%dT%H:%M:%S%z"};
	}
	if (input == "12hour") {
		return {"%I:%M:%S %p"};
	}
	if (input == "24hour") {
		return {"%H:%M:%S"};
	}
	// Not a preset — treat as a format string
	return {input};
}

} // anonymous namespace

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
	// Set opaque type name based on parse mode
	schema_options.opaque_type_name = (result->parse_mode == ParseMode::HTML) ? "HTML" : "XML";
	bool has_explicit_columns = false;

	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
			schema_options.ignore_errors = result->ignore_errors;
		} else if (kv.first == "filename") {
			result->include_filename = kv.second.GetValue<bool>();
		} else if (kv.first == "union_by_name") {
			result->union_by_name = kv.second.GetValue<bool>();
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
		} else if (kv.first == "all_varchar") {
			schema_options.all_varchar = kv.second.GetValue<bool>();
		} else if (kv.first == "datetime_format") {
			// Handle both VARCHAR and LIST(VARCHAR)
			std::vector<std::string> all_formats;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				auto resolved = ResolveDatetimeFormat(kv.second.ToString());
				all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				auto &list_children = ListValue::GetChildren(kv.second);
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						auto resolved = ResolveDatetimeFormat(child.ToString());
						all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
					}
				}
			} else {
				throw BinderException("datetime_format must be a VARCHAR or LIST(VARCHAR)");
			}
			// Validate all format strings
			for (const auto &fmt : all_formats) {
				XMLSchemaInference::ValidateDatetimeFormatString(fmt);
			}
			schema_options.datetime_format_candidates = all_formats;
			// 'auto' is default — only mark explicit if user specified something else
			bool is_auto = false;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR && kv.second.ToString() == "auto") {
				is_auto = true;
			}
			schema_options.has_explicit_datetime_format = !is_auto;
			// If 'none' was specified, disable temporal detection
			if (all_formats.empty()) {
				schema_options.temporal_detection = false;
			}
			// Explicit datetime_format overrides temporal_detection=false
			if (!all_formats.empty()) {
				schema_options.temporal_detection = true;
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

	// Check for conflicting parameters
	if (has_explicit_columns && schema_options.all_varchar) {
		throw BinderException("%s cannot use both \"columns\" parameter and \"all_varchar\" option. "
		                      "Use \"all_varchar\" for automatic schema inference, or specify explicit column types.",
		                      function_name);
	}

	// Store schema options in bind_data for use during execution
	result->schema_options = schema_options;

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			// Determine which files to scan for schema inference
			std::vector<std::string> files_to_scan;
			if (result->union_by_name) {
				// Scan all files to build union schema
				files_to_scan = result->files;
			} else {
				// Scan only first file
				files_to_scan.push_back(result->files[0]);
			}

			// Map to track all unique columns and their types across files
			std::unordered_map<std::string, LogicalType> union_schema;
			std::unordered_map<std::string, std::string> union_formats; // Per-column winning datetime format
			std::vector<std::string> column_order;                      // Track order of first appearance

			// Scan each file for schema
			for (const auto &file_path : files_to_scan) {
				try {
					auto file_handle = fs.OpenFile(file_path, FileFlags::FILE_FLAGS_READ);
					auto file_size = fs.GetFileSize(*file_handle);

					if (file_size > result->max_file_size) {
						if (!result->ignore_errors) {
							throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", file_path,
							                            result->max_file_size);
						}
						continue; // Skip this file
					}

					// Read file content for schema inference
					string content;
					content.resize(file_size);
					file_handle->Read((void *)content.data(), file_size);

					// For XML mode, validate; for HTML mode, be more lenient
					if (mode == ParseMode::XML) {
						if (!XMLUtils::IsValidXML(content)) {
							if (!result->ignore_errors) {
								throw InvalidInputException("File %s contains invalid XML", file_path);
							}
							continue; // Skip this file
						}
					}
					// HTML mode: skip validation, let libxml2's HTML parser handle malformed content

					// Perform schema inference (works for both XML and HTML since both produce xmlDoc)
					auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);

					// Merge columns into union schema
					for (const auto &col_info : inferred_columns) {
						auto it = union_schema.find(col_info.name);
						if (it == union_schema.end()) {
							// New column - add it
							union_schema[col_info.name] = col_info.type;
							union_formats[col_info.name] = col_info.winning_datetime_format;
							column_order.push_back(col_info.name);
						} else {
							// Column exists - check if type needs to be generalized
							// If types differ, use VARCHAR as the most general type
							if (it->second != col_info.type) {
								// For now, use VARCHAR for conflicting types
								// TODO: Implement proper type widening (e.g., INTEGER -> BIGINT -> DOUBLE -> VARCHAR)
								it->second = LogicalType::VARCHAR;
								union_formats[col_info.name] = ""; // No format for VARCHAR fallback
							}
							// NOTE(#38): When types match across files but datetime formats differ,
							// the first file's format wins. This is acceptable since the format was
							// determined by that file's samples and both files parse successfully
							// with it (same type implies compatible format during inference).
						}
					}

				} catch (const Exception &e) {
					if (!result->ignore_errors) {
						throw;
					}
					// Skip this file and continue
				}
			}

			// Convert union schema to return types
			if (!union_schema.empty()) {
				for (const auto &col_name : column_order) {
					names.push_back(col_name);
					return_types.push_back(union_schema[col_name]);
				}

				// Always store schema for execution to ensure consistent column ordering
				// across multiple files (fixes non-determinism from unordered_map iteration)
				result->has_explicit_schema = true;
				result->column_names = names;
				result->column_types = return_types;
				// Store per-column datetime formats for use during extraction
				result->column_datetime_formats.resize(names.size());
				for (size_t i = 0; i < names.size(); i++) {
					auto fmt_it = union_formats.find(names[i]);
					result->column_datetime_formats[i] = (fmt_it != union_formats.end()) ? fmt_it->second : "";
				}
			} else {
				// Fallback to simple schema if no columns were inferred
				if (mode == ParseMode::HTML) {
					return_types.push_back(XMLTypes::HTMLType());
					names.push_back("html");
				} else {
					return_types.push_back(XMLTypes::XMLType());
					names.push_back("xml");
				}
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

	// Add filename column at the beginning if requested
	// Note: Only add to output schema (names/return_types), NOT to stored schema
	// (column_names/column_types) which is used for data extraction
	if (result->include_filename) {
		names.insert(names.begin(), "filename");
		return_types.insert(return_types.begin(), LogicalType::VARCHAR);
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
		// DON'T increment file_index yet - we may not finish this file in one chunk
		const auto &filename = gstate.files[gstate.file_index];

		try {
			// Check if we need to extract rows for current file
			if (gstate.current_file_rows.empty()) {
				// Need to extract rows from current file
				// Check file size
				auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
				auto file_size = fs.GetFileSize(*file_handle);

				if (file_size > bind_data.max_file_size) {
					if (!bind_data.ignore_errors) {
						throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
						                            bind_data.max_file_size);
					}
					// Skip this file - move to next
					gstate.file_index++;
					gstate.current_row_in_file = 0;
					gstate.current_file_rows.clear();
					continue;
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
							// Skip this file - move to next
							gstate.file_index++;
							gstate.current_row_in_file = 0;
							continue;
						} else {
							throw InvalidInputException("File %s contains invalid XML", filename);
						}
					}
				}

				// Extract structured data using appropriate method
				if (bind_data.has_explicit_schema) {
					// Use explicit schema for extraction
					gstate.current_file_rows = XMLSchemaInference::ExtractDataWithSchema(
					    content, bind_data.column_names, bind_data.column_types, schema_options,
					    bind_data.column_datetime_formats);
				} else {
					// Use schema inference
					gstate.current_file_rows = XMLSchemaInference::ExtractData(content, schema_options);
				}

				// Reset row offset for this file
				gstate.current_row_in_file = 0;
			}

			// Fill output vectors with extracted data, starting from current_row_in_file
			while (gstate.current_row_in_file < gstate.current_file_rows.size() && output_idx < STANDARD_VECTOR_SIZE) {
				const auto &row = gstate.current_file_rows[gstate.current_row_in_file];

				// Set values for each column in the row
				idx_t output_col_idx = 0;
				// Output filename as first column if requested
				if (bind_data.include_filename) {
					output.data[output_col_idx++].SetValue(output_idx, Value(filename));
				}
				// Output remaining data columns
				for (idx_t row_col_idx = 0; row_col_idx < row.size() && output_col_idx < output.ColumnCount();
				     row_col_idx++, output_col_idx++) {
					output.data[output_col_idx].SetValue(output_idx, row[row_col_idx]);
				}

				output_idx++;
				gstate.current_row_in_file++;
			}

			// Check if we've finished all rows from this file
			if (gstate.current_row_in_file >= gstate.current_file_rows.size()) {
				// Finished this file - move to next file
				gstate.file_index++;
				gstate.current_row_in_file = 0;
				gstate.current_file_rows.clear(); // Free memory
			}

		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue - move to next file
			gstate.file_index++;
			gstate.current_row_in_file = 0;
			gstate.current_file_rows.clear();
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
	result->parse_mode = ParseMode::XML;

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
	// Set opaque type name based on parse mode
	schema_options.opaque_type_name = (result->parse_mode == ParseMode::HTML) ? "HTML" : "XML";
	bool has_explicit_columns = false;

	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
			schema_options.ignore_errors = result->ignore_errors;
		} else if (kv.first == "filename") {
			result->include_filename = kv.second.GetValue<bool>();
		} else if (kv.first == "union_by_name") {
			result->union_by_name = kv.second.GetValue<bool>();
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
		} else if (kv.first == "all_varchar") {
			schema_options.all_varchar = kv.second.GetValue<bool>();
		} else if (kv.first == "datetime_format") {
			// Handle both VARCHAR and LIST(VARCHAR)
			std::vector<std::string> all_formats;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				auto resolved = ResolveDatetimeFormat(kv.second.ToString());
				all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				auto &list_children = ListValue::GetChildren(kv.second);
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						auto resolved = ResolveDatetimeFormat(child.ToString());
						all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
					}
				}
			} else {
				throw BinderException("datetime_format must be a VARCHAR or LIST(VARCHAR)");
			}
			// Validate all format strings
			for (const auto &fmt : all_formats) {
				XMLSchemaInference::ValidateDatetimeFormatString(fmt);
			}
			schema_options.datetime_format_candidates = all_formats;
			// 'auto' is default — only mark explicit if user specified something else
			bool is_auto = false;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR && kv.second.ToString() == "auto") {
				is_auto = true;
			}
			schema_options.has_explicit_datetime_format = !is_auto;
			// If 'none' was specified, disable temporal detection
			if (all_formats.empty()) {
				schema_options.temporal_detection = false;
			}
			// Explicit datetime_format overrides temporal_detection=false
			if (!all_formats.empty()) {
				schema_options.temporal_detection = true;
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

	// Check for conflicting parameters
	if (has_explicit_columns && schema_options.all_varchar) {
		throw BinderException("read_xml cannot use both \"columns\" parameter and \"all_varchar\" option. "
		                      "Use \"all_varchar\" for automatic schema inference, or specify explicit column types.");
	}

	// Store schema options in bind_data for use during execution
	result->schema_options = schema_options;

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			// Determine which files to scan for schema inference
			std::vector<std::string> files_to_scan;
			if (result->union_by_name) {
				// Scan all files to build union schema
				files_to_scan = result->files;
			} else {
				// Scan only first file
				files_to_scan.push_back(result->files[0]);
			}

			// Map to track all unique columns and their types across files
			std::unordered_map<std::string, LogicalType> union_schema;
			std::unordered_map<std::string, std::string> union_formats; // Per-column winning datetime format
			std::vector<std::string> column_order;                      // Track order of first appearance

			// Scan each file for schema
			for (const auto &file_path : files_to_scan) {
				try {
					auto file_handle = fs.OpenFile(file_path, FileFlags::FILE_FLAGS_READ);
					auto file_size = fs.GetFileSize(*file_handle);

					if (file_size > result->max_file_size) {
						if (!result->ignore_errors) {
							throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", file_path,
							                            result->max_file_size);
						}
						continue; // Skip this file
					}

					// Read file content for schema inference
					string content;
					content.resize(file_size);
					file_handle->Read((void *)content.data(), file_size);

					// Validate XML
					if (!XMLUtils::IsValidXML(content)) {
						if (!result->ignore_errors) {
							throw InvalidInputException("File %s contains invalid XML", file_path);
						}
						continue; // Skip this file
					}

					// Perform schema inference
					auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);

					// Merge columns into union schema
					for (const auto &col_info : inferred_columns) {
						auto it = union_schema.find(col_info.name);
						if (it == union_schema.end()) {
							// New column - add it
							union_schema[col_info.name] = col_info.type;
							union_formats[col_info.name] = col_info.winning_datetime_format;
							column_order.push_back(col_info.name);
						} else {
							// Column exists - check if type needs to be generalized
							// If types differ, use VARCHAR as the most general type
							if (it->second != col_info.type) {
								// For now, use VARCHAR for conflicting types
								// TODO: Implement proper type widening (e.g., INTEGER -> BIGINT -> DOUBLE -> VARCHAR)
								it->second = LogicalType::VARCHAR;
								union_formats[col_info.name] = ""; // No format for VARCHAR fallback
							}
							// NOTE(#38): When types match across files but datetime formats differ,
							// the first file's format wins. This is acceptable since the format was
							// determined by that file's samples and both files parse successfully
							// with it (same type implies compatible format during inference).
						}
					}

				} catch (const Exception &e) {
					if (!result->ignore_errors) {
						throw;
					}
					// Skip this file and continue
				}
			}

			// Convert union schema to return types
			if (!union_schema.empty()) {
				for (const auto &col_name : column_order) {
					names.push_back(col_name);
					return_types.push_back(union_schema[col_name]);
				}

				// Always store schema for execution to ensure consistent column ordering
				// across multiple files (fixes non-determinism from unordered_map iteration)
				result->has_explicit_schema = true;
				result->column_names = names;
				result->column_types = return_types;
				// Store per-column datetime formats for use during extraction
				result->column_datetime_formats.resize(names.size());
				for (size_t i = 0; i < names.size(); i++) {
					auto fmt_it = union_formats.find(names[i]);
					result->column_datetime_formats[i] = (fmt_it != union_formats.end()) ? fmt_it->second : "";
				}
			} else {
				// Fallback to simple schema if no columns were inferred
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
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

	// Add filename column at the beginning if requested
	// Note: Only add to output schema (names/return_types), NOT to stored schema
	// (column_names/column_types) which is used for data extraction
	if (result->include_filename) {
		names.insert(names.begin(), "filename");
		return_types.insert(return_types.begin(), LogicalType::VARCHAR);
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
		// DON'T increment file_index yet - we may not finish this file in one chunk
		const auto &filename = gstate.files[gstate.file_index];

		try {
			// Check if we need to extract rows for current file
			if (gstate.current_file_rows.empty()) {
				// Need to extract rows from current file
				// Check file size
				auto file_handle = fs.OpenFile(filename, FileFlags::FILE_FLAGS_READ);
				auto file_size = fs.GetFileSize(*file_handle);

				if (file_size > bind_data.max_file_size) {
					if (!bind_data.ignore_errors) {
						throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
						                            bind_data.max_file_size);
					}
					// Skip this file - move to next
					gstate.file_index++;
					gstate.current_row_in_file = 0;
					gstate.current_file_rows.clear();
					continue;
				}

				// Read file content
				string content;
				content.resize(file_size);
				file_handle->Read((void *)content.data(), file_size);

				// Validate XML
				if (!XMLUtils::IsValidXML(content)) {
					if (bind_data.ignore_errors) {
						// Skip this file - move to next
						gstate.file_index++;
						gstate.current_row_in_file = 0;
						gstate.current_file_rows.clear();
						continue;
					} else {
						throw InvalidInputException("File %s contains invalid XML", filename);
					}
				}

				// Extract structured data using appropriate method
				if (bind_data.has_explicit_schema) {
					// Use explicit schema for extraction
					gstate.current_file_rows = XMLSchemaInference::ExtractDataWithSchema(
					    content, bind_data.column_names, bind_data.column_types, schema_options,
					    bind_data.column_datetime_formats);
				} else {
					// Use schema inference
					gstate.current_file_rows = XMLSchemaInference::ExtractData(content, schema_options);
				}

				// Reset row offset for this file
				gstate.current_row_in_file = 0;
			}

			// Fill output vectors with extracted data, starting from current_row_in_file
			while (gstate.current_row_in_file < gstate.current_file_rows.size() && output_idx < STANDARD_VECTOR_SIZE) {
				const auto &row = gstate.current_file_rows[gstate.current_row_in_file];

				// Set values for each column in the row
				idx_t output_col_idx = 0;
				// Output filename as first column if requested
				if (bind_data.include_filename) {
					output.data[output_col_idx++].SetValue(output_idx, Value(filename));
				}
				// Output remaining data columns
				for (idx_t row_col_idx = 0; row_col_idx < row.size() && output_col_idx < output.ColumnCount();
				     row_col_idx++, output_col_idx++) {
					output.data[output_col_idx].SetValue(output_idx, row[row_col_idx]);
				}

				output_idx++;
				gstate.current_row_in_file++;
			}

			// Check if we've finished all rows from this file
			if (gstate.current_row_in_file >= gstate.current_file_rows.size()) {
				// Finished this file - move to next file
				gstate.file_index++;
				gstate.current_row_in_file = 0;
				gstate.current_file_rows.clear(); // Free memory
			}

		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue - move to next file
			gstate.file_index++;
			gstate.current_row_in_file = 0;
			gstate.current_file_rows.clear();
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

// =============================================================================
// String-based Parse Functions (parse_xml, parse_html, etc.)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ParseDocumentObjectsBind(ClientContext &context,
                                                                      TableFunctionBindInput &input,
                                                                      vector<LogicalType> &return_types,
                                                                      vector<string> &names, ParseMode mode) {
	auto result = make_uniq<XMLParseData>();
	result->parse_mode = mode;

	const char *function_name = (mode == ParseMode::HTML) ? "parse_html_objects" : "parse_xml_objects";

	// Get XML/HTML content from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("%s requires XML/HTML content as first argument", function_name);
	}

	result->xml_content = input.inputs[0].ToString();

	// Handle optional parameters
	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
		}
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

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ParseDocumentObjectsInit(ClientContext &context,
                                                                                  TableFunctionInitInput &input) {
	auto result = make_uniq<XMLParseGlobalState>();
	auto &bind_data = input.bind_data->Cast<XMLParseData>();

	// For _objects functions, we just return the content as a single row
	// Validation is done at execution time
	result->current_row = 0;

	return std::move(result);
}

void XMLReaderFunctions::ParseDocumentObjectsFunction(ClientContext &context, TableFunctionInput &data_p,
                                                      DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLParseData>();
	auto &gstate = data_p.global_state->Cast<XMLParseGlobalState>();

	// Only return one row
	if (gstate.current_row > 0) {
		return;
	}

	const bool is_html = (bind_data.parse_mode == ParseMode::HTML);
	const string &content = bind_data.xml_content;

	// Validate content based on mode
	if (is_html) {
		// HTML mode: be lenient, allow empty content
		if (content.empty()) {
			if (bind_data.ignore_errors) {
				output.SetCardinality(0);
				return;
			}
			// Return minimal valid HTML for empty input
			output.data[0].SetValue(0, Value("<html></html>"));
			output.SetCardinality(1);
			gstate.current_row = 1;
			return;
		}
	} else {
		// XML mode: strict validation
		if (!XMLUtils::IsValidXML(content)) {
			if (bind_data.ignore_errors) {
				output.SetCardinality(0);
				return;
			}
			throw InvalidInputException("Input contains invalid XML");
		}
	}

	// Return the content
	output.data[0].SetValue(0, Value(content));
	output.SetCardinality(1);
	gstate.current_row = 1;
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseDocumentBind(ClientContext &context, TableFunctionBindInput &input,
                                                               vector<LogicalType> &return_types, vector<string> &names,
                                                               ParseMode mode) {
	auto result = make_uniq<XMLParseData>();
	result->parse_mode = mode;

	const char *function_name = (mode == ParseMode::HTML) ? "parse_html" : "parse_xml";

	// Get XML/HTML content from first argument
	if (input.inputs.empty()) {
		throw InvalidInputException("%s requires XML/HTML content as first argument", function_name);
	}

	result->xml_content = input.inputs[0].ToString();

	// Handle optional parameters with schema inference defaults
	XMLSchemaOptions schema_options;
	schema_options.opaque_type_name = (mode == ParseMode::HTML) ? "HTML" : "XML";
	bool has_explicit_columns = false;

	for (auto &kv : input.named_parameters) {
		if (kv.first == "ignore_errors") {
			result->ignore_errors = kv.second.GetValue<bool>();
			schema_options.ignore_errors = result->ignore_errors;
		} else if (kv.first == "root_element") {
			schema_options.root_element = kv.second.ToString();
		} else if (kv.first == "record_element") {
			std::string record_value = kv.second.ToString();
			if (record_value.find('/') == std::string::npos) {
				record_value = "//" + record_value;
			}
			schema_options.record_element = record_value;
		} else if (kv.first == "force_list") {
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				std::string force_value = kv.second.ToString();
				if (force_value.find('/') == std::string::npos) {
					force_value = "//" + force_value;
				}
				schema_options.force_list = force_value;
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				auto &list_children = ListValue::GetChildren(kv.second);
				std::vector<std::string> xpaths;
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						std::string tag = child.ToString();
						if (tag.find('/') == std::string::npos) {
							tag = "//" + tag;
						}
						xpaths.push_back(tag);
					}
				}
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
				schema_options.unnest_as_columns = false;
			} else {
				throw BinderException("%s \"unnest_as\" parameter must be 'columns' or 'struct', got: '%s'",
				                      function_name, unnest_mode);
			}
		} else if (kv.first == "all_varchar") {
			schema_options.all_varchar = kv.second.GetValue<bool>();
		} else if (kv.first == "datetime_format") {
			// Handle both VARCHAR and LIST(VARCHAR)
			std::vector<std::string> all_formats;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				auto resolved = ResolveDatetimeFormat(kv.second.ToString());
				all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				auto &list_children = ListValue::GetChildren(kv.second);
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						auto resolved = ResolveDatetimeFormat(child.ToString());
						all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
					}
				}
			} else {
				throw BinderException("datetime_format must be a VARCHAR or LIST(VARCHAR)");
			}
			// Validate all format strings
			for (const auto &fmt : all_formats) {
				XMLSchemaInference::ValidateDatetimeFormatString(fmt);
			}
			schema_options.datetime_format_candidates = all_formats;
			// 'auto' is default — only mark explicit if user specified something else
			bool is_auto = false;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR && kv.second.ToString() == "auto") {
				is_auto = true;
			}
			schema_options.has_explicit_datetime_format = !is_auto;
			// If 'none' was specified, disable temporal detection
			if (all_formats.empty()) {
				schema_options.temporal_detection = false;
			}
			// Explicit datetime_format overrides temporal_detection=false
			if (!all_formats.empty()) {
				schema_options.temporal_detection = true;
			}
		} else if (kv.first == "columns") {
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

				auto logical_type = TransformStringToLogicalType(StringValue::Get(val), context);
				return_types.push_back(logical_type);
				names.push_back(name);
			}

			if (return_types.empty()) {
				throw BinderException("%s \"columns\" parameter needs at least one column.", function_name);
			}

			result->has_explicit_schema = true;
			result->column_names = names;
			result->column_types = return_types;
			has_explicit_columns = true;
		}
	}

	// Check for conflicting parameters
	if (has_explicit_columns && schema_options.all_varchar) {
		throw BinderException("%s cannot use both \"columns\" parameter and \"all_varchar\" option.", function_name);
	}

	result->schema_options = schema_options;

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		try {
			const string &content = result->xml_content;

			// Validate content based on mode
			if (mode == ParseMode::XML) {
				if (!XMLUtils::IsValidXML(content)) {
					if (!result->ignore_errors) {
						throw InvalidInputException("Input contains invalid XML");
					}
					// Fallback to simple schema
					return_types.push_back(XMLTypes::XMLType());
					names.push_back("xml");
					return std::move(result);
				}
			}
			// HTML mode: skip validation, let libxml2's HTML parser handle it

			// Perform schema inference
			auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);

			if (!inferred_columns.empty()) {
				for (const auto &col_info : inferred_columns) {
					names.push_back(col_info.name);
					return_types.push_back(col_info.type);
				}

				result->has_explicit_schema = true;
				result->column_names = names;
				result->column_types = return_types;
				// Store per-column datetime formats for use during extraction
				result->column_datetime_formats.resize(inferred_columns.size());
				for (size_t i = 0; i < inferred_columns.size(); i++) {
					result->column_datetime_formats[i] = inferred_columns[i].winning_datetime_format;
				}
			} else {
				// Fallback to simple schema
				if (mode == ParseMode::HTML) {
					return_types.push_back(XMLTypes::HTMLType());
					names.push_back("html");
				} else {
					return_types.push_back(XMLTypes::XMLType());
					names.push_back("xml");
				}
			}

		} catch (const Exception &e) {
			if (!result->ignore_errors) {
				throw;
			}
			// Fallback to simple schema
			if (mode == ParseMode::HTML) {
				return_types.push_back(XMLTypes::HTMLType());
				names.push_back("html");
			} else {
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
			}
		}

		// Ensure we have at least one column
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

unique_ptr<GlobalTableFunctionState> XMLReaderFunctions::ParseDocumentInit(ClientContext &context,
                                                                           TableFunctionInitInput &input) {
	auto result = make_uniq<XMLParseGlobalState>();
	auto &bind_data = input.bind_data->Cast<XMLParseData>();

	// Extract data at init time
	const string &content = bind_data.xml_content;
	const auto &schema_options = bind_data.schema_options;

	try {
		if (bind_data.has_explicit_schema) {
			result->extracted_rows =
			    XMLSchemaInference::ExtractDataWithSchema(content, bind_data.column_names, bind_data.column_types,
			                                              schema_options, bind_data.column_datetime_formats);
		} else {
			result->extracted_rows = XMLSchemaInference::ExtractData(content, schema_options);
		}
	} catch (const Exception &e) {
		if (!bind_data.ignore_errors) {
			throw;
		}
		// Return empty result set on error
		result->extracted_rows.clear();
	}

	result->current_row = 0;
	return std::move(result);
}

void XMLReaderFunctions::ParseDocumentFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &gstate = data_p.global_state->Cast<XMLParseGlobalState>();

	idx_t output_idx = 0;

	while (output_idx < STANDARD_VECTOR_SIZE && gstate.current_row < gstate.extracted_rows.size()) {
		const auto &row = gstate.extracted_rows[gstate.current_row];

		for (idx_t col_idx = 0; col_idx < row.size() && col_idx < output.ColumnCount(); col_idx++) {
			output.data[col_idx].SetValue(output_idx, row[col_idx]);
		}

		output_idx++;
		gstate.current_row++;
	}

	output.SetCardinality(output_idx);
}

// Public parse_xml functions (delegate to internal functions)
unique_ptr<FunctionData> XMLReaderFunctions::ParseXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                 vector<LogicalType> &return_types,
                                                                 vector<string> &names) {
	return ParseDocumentObjectsBind(context, input, return_types, names, ParseMode::XML);
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseXMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                          vector<LogicalType> &return_types, vector<string> &names) {
	return ParseDocumentBind(context, input, return_types, names, ParseMode::XML);
}

// Public parse_html functions (delegate to internal functions)
unique_ptr<FunctionData> XMLReaderFunctions::ParseHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                  vector<LogicalType> &return_types,
                                                                  vector<string> &names) {
	return ParseDocumentObjectsBind(context, input, return_types, names, ParseMode::HTML);
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseHTMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types, vector<string> &names) {
	return ParseDocumentBind(context, input, return_types, names, ParseMode::HTML);
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
	read_xml_single.named_parameters["union_by_name"] = LogicalType::BOOLEAN;
	read_xml_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	// Schema inference parameters
	read_xml_single.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_xml_single.named_parameters["attr_mode"] = LogicalType::VARCHAR; // 'columns' | 'prefixed' | 'map' | 'discard'
	read_xml_single.named_parameters["attr_prefix"] =
	    LogicalType::VARCHAR; // Prefix for attributes when attr_mode='prefixed'
	read_xml_single.named_parameters["text_key"] = LogicalType::VARCHAR;       // Key for mixed text content
	read_xml_single.named_parameters["namespaces"] = LogicalType::VARCHAR;     // 'strip' | 'expand' | 'keep'
	read_xml_single.named_parameters["empty_elements"] = LogicalType::VARCHAR; // 'null' | 'string' | 'object'
	read_xml_single.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_xml_single.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_xml_single.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_xml_single.named_parameters["record_element"] =
	    LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_xml_single.named_parameters["force_list"] =
	    LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_xml_single.named_parameters["columns"] = LogicalType::ANY;
	read_xml_single.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	read_xml_single.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
	read_xml_set.AddFunction(read_xml_single);

	// Variant 2: Array of strings parameter
	TableFunction read_xml_array("read_xml", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadXMLFunction, ReadXMLBind,
	                             ReadXMLInit);
	read_xml_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_xml_array.named_parameters["union_by_name"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	// Schema inference parameters
	read_xml_array.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_xml_array.named_parameters["attr_mode"] = LogicalType::VARCHAR; // 'columns' | 'prefixed' | 'map' | 'discard'
	read_xml_array.named_parameters["attr_prefix"] =
	    LogicalType::VARCHAR; // Prefix for attributes when attr_mode='prefixed'
	read_xml_array.named_parameters["text_key"] = LogicalType::VARCHAR;       // Key for mixed text content
	read_xml_array.named_parameters["namespaces"] = LogicalType::VARCHAR;     // 'strip' | 'expand' | 'keep'
	read_xml_array.named_parameters["empty_elements"] = LogicalType::VARCHAR; // 'null' | 'string' | 'object'
	read_xml_array.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_xml_array.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_xml_array.named_parameters["record_element"] =
	    LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_xml_array.named_parameters["force_list"] =
	    LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_xml_array.named_parameters["columns"] = LogicalType::ANY;
	read_xml_array.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	read_xml_array.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
	read_xml_set.AddFunction(read_xml_array);

	loader.RegisterFunction(read_xml_set);

	// Register read_html table function for reading HTML files (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_html_set("read_html");

	// Variant 1: Single string parameter
	TableFunction read_html_single("read_html", {LogicalType::VARCHAR}, ReadHTMLFunction, ReadHTMLBind, ReadHTMLInit);
	read_html_single.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_single.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_single.named_parameters["union_by_name"] = LogicalType::BOOLEAN;
	read_html_single.named_parameters["filename"] = LogicalType::BOOLEAN;
	// Schema inference parameters (same as read_xml for API consistency)
	read_html_single.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_html_single.named_parameters["attr_mode"] = LogicalType::VARCHAR; // 'columns' | 'prefixed' | 'map' | 'discard'
	read_html_single.named_parameters["attr_prefix"] =
	    LogicalType::VARCHAR; // Prefix for attributes when attr_mode='prefixed'
	read_html_single.named_parameters["text_key"] = LogicalType::VARCHAR;       // Key for mixed text content
	read_html_single.named_parameters["namespaces"] = LogicalType::VARCHAR;     // 'strip' | 'expand' | 'keep'
	read_html_single.named_parameters["empty_elements"] = LogicalType::VARCHAR; // 'null' | 'string' | 'object'
	read_html_single.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_html_single.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_html_single.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_html_single.named_parameters["record_element"] =
	    LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_html_single.named_parameters["force_list"] =
	    LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_html_single.named_parameters["columns"] = LogicalType::ANY;
	read_html_single.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	read_html_single.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
	read_html_set.AddFunction(read_html_single);

	// Variant 2: Array of strings parameter
	TableFunction read_html_array("read_html", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadHTMLFunction,
	                              ReadHTMLBind, ReadHTMLInit);
	read_html_array.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	read_html_array.named_parameters["maximum_file_size"] = LogicalType::BIGINT;
	read_html_array.named_parameters["union_by_name"] = LogicalType::BOOLEAN;
	read_html_array.named_parameters["filename"] = LogicalType::BOOLEAN;
	// Schema inference parameters (same as read_xml for API consistency)
	read_html_array.named_parameters["root_element"] = LogicalType::VARCHAR;
	read_html_array.named_parameters["attr_mode"] = LogicalType::VARCHAR; // 'columns' | 'prefixed' | 'map' | 'discard'
	read_html_array.named_parameters["attr_prefix"] =
	    LogicalType::VARCHAR; // Prefix for attributes when attr_mode='prefixed'
	read_html_array.named_parameters["text_key"] = LogicalType::VARCHAR;       // Key for mixed text content
	read_html_array.named_parameters["namespaces"] = LogicalType::VARCHAR;     // 'strip' | 'expand' | 'keep'
	read_html_array.named_parameters["empty_elements"] = LogicalType::VARCHAR; // 'null' | 'string' | 'object'
	read_html_array.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	read_html_array.named_parameters["max_depth"] = LogicalType::INTEGER;
	read_html_array.named_parameters["unnest_as"] = LogicalType::VARCHAR; // 'columns' (default) or 'struct' (future)
	read_html_array.named_parameters["record_element"] =
	    LogicalType::VARCHAR; // XPath or tag name for elements that should be rows
	read_html_array.named_parameters["force_list"] =
	    LogicalType::ANY; // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	// Explicit schema specification (like JSON extension)
	read_html_array.named_parameters["columns"] = LogicalType::ANY;
	read_html_array.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	read_html_array.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
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

	// =============================================================================
	// Register parse_xml_objects table function (parses XML string, returns raw content)
	// =============================================================================
	TableFunction parse_xml_objects("parse_xml_objects", {LogicalType::VARCHAR}, ParseDocumentObjectsFunction,
	                                ParseXMLObjectsBind, ParseDocumentObjectsInit);
	parse_xml_objects.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	loader.RegisterFunction(parse_xml_objects);

	// =============================================================================
	// Register parse_html_objects table function (parses HTML string, returns raw content)
	// =============================================================================
	TableFunction parse_html_objects("parse_html_objects", {LogicalType::VARCHAR}, ParseDocumentObjectsFunction,
	                                 ParseHTMLObjectsBind, ParseDocumentObjectsInit);
	parse_html_objects.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	loader.RegisterFunction(parse_html_objects);

	// =============================================================================
	// Register parse_xml table function (parses XML string with schema inference)
	// =============================================================================
	TableFunction parse_xml("parse_xml", {LogicalType::VARCHAR}, ParseDocumentFunction, ParseXMLBind,
	                        ParseDocumentInit);
	parse_xml.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	// Schema inference parameters (same as read_xml)
	parse_xml.named_parameters["root_element"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["record_element"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["force_list"] = LogicalType::ANY;
	parse_xml.named_parameters["attr_mode"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["attr_prefix"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["text_key"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["namespaces"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["empty_elements"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	parse_xml.named_parameters["max_depth"] = LogicalType::INTEGER;
	parse_xml.named_parameters["unnest_as"] = LogicalType::VARCHAR;
	parse_xml.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	parse_xml.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
	parse_xml.named_parameters["columns"] = LogicalType::ANY;
	loader.RegisterFunction(parse_xml);

	// =============================================================================
	// Register parse_html table function (parses HTML string with schema inference)
	// =============================================================================
	TableFunction parse_html("parse_html", {LogicalType::VARCHAR}, ParseDocumentFunction, ParseHTMLBind,
	                         ParseDocumentInit);
	parse_html.named_parameters["ignore_errors"] = LogicalType::BOOLEAN;
	// Schema inference parameters (same as read_html)
	parse_html.named_parameters["root_element"] = LogicalType::VARCHAR;
	parse_html.named_parameters["record_element"] = LogicalType::VARCHAR;
	parse_html.named_parameters["force_list"] = LogicalType::ANY;
	parse_html.named_parameters["attr_mode"] = LogicalType::VARCHAR;
	parse_html.named_parameters["attr_prefix"] = LogicalType::VARCHAR;
	parse_html.named_parameters["text_key"] = LogicalType::VARCHAR;
	parse_html.named_parameters["namespaces"] = LogicalType::VARCHAR;
	parse_html.named_parameters["empty_elements"] = LogicalType::VARCHAR;
	parse_html.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
	parse_html.named_parameters["max_depth"] = LogicalType::INTEGER;
	parse_html.named_parameters["unnest_as"] = LogicalType::VARCHAR;
	parse_html.named_parameters["all_varchar"] = LogicalType::BOOLEAN;
	parse_html.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
	parse_html.named_parameters["columns"] = LogicalType::ANY;
	loader.RegisterFunction(parse_html);
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
