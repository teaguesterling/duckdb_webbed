#include "xml_reader_functions.hpp"
#include "named_parameter_compat.hpp"
#include "xml_utils.hpp"
#include "xml_file_io.hpp"
#include "xml_schema_inference.hpp"
#include "xml_types.hpp"
#include "duckdb_compat.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/execution/partition_info.hpp" // OperatorPartitionData (batch-index tagging, issue #72)
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/replacement_scan.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/common/multi_file/multi_file_reader.hpp"
#include "duckdb/function/scalar/strftime_format.hpp"
#include "duckdb/parallel/task_executor.hpp"
#include "duckdb/parallel/task_scheduler.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/main/config.hpp"

#include <algorithm>
#include <exception>

namespace {

// Check if a record_element value contains XPath syntax that SAX mode can't handle.
// SAX only supports simple tag names (e.g., "item"), optionally prefixed with "//".
// Path expressions ("/root/item"), predicates ("item[@type]"), axes ("child::item"),
// and functions ("local-name()") all require DOM XPath evaluation.
static bool HasComplexXPath(const std::string &record_element) {
	if (record_element.empty()) {
		return false;
	}
	// Strip leading "//" (SAX-compatible shorthand)
	std::string re = record_element;
	if (re.size() >= 2 && re[0] == '/' && re[1] == '/') {
		re = re.substr(2);
	}
	// Any remaining XPath tokens mean SAX can't handle it
	return re.find_first_of("/[@]()*:.") != std::string::npos;
}

// Raise the error used when libxml2 cannot allocate memory while parsing a file. A
// libxml2 allocation failure is a transient resource condition, not a per-file data
// quality problem, so callers throw this regardless of ignore_errors and let the
// per-file catch blocks re-raise OutOfMemoryException so the scan fails loudly.
[[noreturn]] static void ThrowLibxmlOutOfMemory(const std::string &file_path) {
	throw duckdb::OutOfMemoryException(
	    "Failed to parse file \"%s\": libxml2 could not allocate memory (the system may be under memory pressure)",
	    file_path);
}

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

// Parse empty_elements: how an element with no text, children or attributes types its column.
// 'null' and 'object' leave the type to other instances or files (VARCHAR if none has one).
// 'string' types the column VARCHAR.
static std::string ParseEmptyElementsParameter(const Value &val, const char *function_name) {
	if (val.IsNull()) {
		return XMLSchemaOptions {}.empty_elements;
	}
	auto mode = val.ToString();
	if (mode != "null" && mode != "object" && mode != "string") {
		throw BinderException("%s \"empty_elements\" parameter must be 'null', 'object' or 'string', got: '%s'",
		                      function_name, mode);
	}
	return mode;
}

// Parse nullstr parameter (VARCHAR or LIST(VARCHAR)) into null_strings vector.
// Throws BinderException for invalid types.
static void ParseNullstrParameter(const Value &val, const char *function_name, std::vector<std::string> &null_strings) {
	auto type_id = val.type().id();
	if (type_id == LogicalTypeId::VARCHAR) {
		null_strings.push_back(val.ToString());
	} else if (type_id == LogicalTypeId::LIST) {
		auto &list_children = ListValue::GetChildren(val);
		for (const auto &child : list_children) {
			if (child.IsNull()) {
				continue;
			}
			if (child.type().id() != LogicalTypeId::VARCHAR) {
				throw BinderException("%s \"nullstr\" list elements must be VARCHAR, got: %s", function_name,
				                      child.type().ToString());
			}
			null_strings.push_back(child.ToString());
		}
	} else {
		throw BinderException("%s \"nullstr\" parameter must be VARCHAR or LIST(VARCHAR), got: %s", function_name,
		                      val.type().ToString());
	}
}

// True if a column type contains MAP or ARRAY at any nesting. These DuckDB types have no
// XML extraction path, so SAX cannot materialize them. STRUCT and LIST<STRUCT> are supported
// through the DOM extraction of each streamed record.
static bool ContainsUnsupportedSAXType(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::MAP:
	case LogicalTypeId::ARRAY:
		return true;
	case LogicalTypeId::LIST:
		return ContainsUnsupportedSAXType(ListType::GetChildType(type));
	case LogicalTypeId::STRUCT: {
		for (auto &child : StructType::GetChildTypes(type)) {
			if (ContainsUnsupportedSAXType(child.second)) {
				return true;
			}
		}
		return false;
	}
	default:
		return false;
	}
}

static bool SchemaHasUnsupportedSAXType(const std::vector<LogicalType> &column_types) {
	for (const auto &t : column_types) {
		if (ContainsUnsupportedSAXType(t)) {
			return true;
		}
	}
	return false;
}

// Merge per-file inferred columns into the running union schema for union_by_name.
// New columns are appended; existing columns are widened via MergeXMLColumnType.
// When the merged type differs from the previous one, the column's datetime format
// hint is reset so that DuckDB's built-in temporal parsing handles the widened type.
// An unknown side (SQLNULL, or a list of it: an element empty so far) carries no format, so the
// other side's is kept.
static bool IsUnknownElementType(const LogicalType &type) {
	if (type.id() == LogicalTypeId::LIST) {
		return IsUnknownElementType(ListType::GetChildType(type));
	}
	return type.id() == LogicalTypeId::SQLNULL;
}

static void MergeInferredColumns(const std::vector<XMLColumnInfo> &inferred_columns,
                                 std::unordered_map<std::string, LogicalType> &union_schema,
                                 std::unordered_map<std::string, std::string> &union_formats,
                                 std::vector<std::string> &column_order, const std::string &text_key) {
	for (const auto &col_info : inferred_columns) {
		auto it = union_schema.find(col_info.name);
		if (it == union_schema.end()) {
			union_schema[col_info.name] = col_info.type;
			union_formats[col_info.name] = col_info.winning_datetime_format;
			column_order.push_back(col_info.name);
			continue;
		}
		auto merged = XMLSchemaInference::MergeXMLColumnType(it->second, col_info.type, text_key);
		if (merged != it->second) {
			if (IsUnknownElementType(it->second)) {
				union_formats[col_info.name] = col_info.winning_datetime_format;
			} else if (!IsUnknownElementType(col_info.type)) {
				union_formats[col_info.name] = "";
			}
			it->second = merged;
		}
		// NOTE(#38): When types match across files but datetime formats differ,
		// the first file's format wins. This is acceptable since the format was
		// determined by that file's samples and both files parse successfully
		// with it (same type implies compatible format during inference).
	}
}

// Infer one file's columns at bind. Returns false when ignore_errors skips the file (too large
// for DOM, or invalid XML); throws on any other failure.
static bool InferFileColumns(FileSystem &fs, const std::string &file_path, const XMLReadFunctionData &bind_data,
                             const XMLSchemaOptions &schema_options, ParseMode mode,
                             std::vector<XMLColumnInfo> &inferred_columns) {
	auto handle = OpenXMLFile(fs, file_path, bind_data.compression);
	// A large XML file parses straight from its handle, so its bytes never sit beside the DOM
	bool parse_from_handle =
	    mode == ParseMode::XML && schema_options.opaque_type_name != "HTML" && ParsesFromHandle(*handle);
	string content;
	bool fits = parse_from_handle ? handle->GetFileSize() <= bind_data.max_file_size
	                              : ReadXMLFile(*handle, bind_data.max_file_size, content);
	if (!fits) {
		string().swap(content);
		if (schema_options.streaming && mode == ParseMode::XML && !HasComplexXPath(schema_options.record_element)) {
			inferred_columns = SAXStreamReader::InferSchemaFromStream(fs, file_path, schema_options);
			return true;
		}
		if (!bind_data.ignore_errors) {
			throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", file_path,
			                            bind_data.max_file_size);
		}
		return false;
	}

	if (mode != ParseMode::XML) {
		inferred_columns = XMLSchemaInference::InferSchemaForMerge(content, schema_options);
		return true;
	}
	// One parse serves both the validity check (CheckXML parses identically) and inference,
	// instead of parsing the file twice at bind.
	XMLDocRAII doc = parse_from_handle ? ParseXMLFile(*handle, false) : XMLDocRAII(content);
	if (!doc.IsValid()) {
		if (doc.HadResourceError()) {
			ThrowLibxmlOutOfMemory(file_path);
		}
		if (!bind_data.ignore_errors) {
			throw InvalidInputException("File %s contains invalid XML", file_path);
		}
		return false;
	}
	if (schema_options.opaque_type_name == "HTML") {
		inferred_columns = XMLSchemaInference::InferSchemaForMerge(content, schema_options);
	} else {
		string().swap(content);
		inferred_columns = XMLSchemaInference::InferSchemaFromDocument(doc, schema_options);
	}
	return true;
}

// One file's bind-time inference result. A file skipped under ignore_errors has neither columns
// nor an error.
struct FileInferenceResult {
	bool inferred = false;
	std::vector<XMLColumnInfo> columns;
	std::exception_ptr error;
	//! The error ends the bind even under ignore_errors (out of memory, or not a DuckDB exception)
	bool error_is_fatal = false;
};

struct FileInferenceState {
	FileInferenceState(FileSystem &fs, const std::vector<std::string> &files, const XMLReadFunctionData &bind_data,
	                   const XMLSchemaOptions &schema_options, ParseMode mode)
	    : fs(fs), files(files), bind_data(bind_data), schema_options(schema_options), mode(mode), results(files.size()),
	      first_fatal_idx(files.size()) {
	}

	FileSystem &fs;
	const std::vector<std::string> &files;
	const XMLReadFunctionData &bind_data;
	const XMLSchemaOptions &schema_options;
	ParseMode mode;
	std::vector<FileInferenceResult> results;
	//! Next file for a task to claim. Files are claimed in ascending order.
	atomic<idx_t> next_file_idx {0};
	//! Lowest file index whose error the bind rethrows. Later files can't affect the outcome, so tasks
	//! stop claiming once they pass it.
	atomic<idx_t> first_fatal_idx;

	void MarkFatal(idx_t file_idx) {
		auto current = first_fatal_idx.load();
		while (file_idx < current && !first_fatal_idx.compare_exchange_weak(current, file_idx)) {
		}
	}
};

class FileInferenceTask : public BaseExecutorTask {
public:
	FileInferenceTask(TaskExecutor &executor, FileInferenceState &state) : BaseExecutorTask(executor), state(state) {
	}

	void ExecuteTask() override {
		for (auto file_idx = state.next_file_idx++; file_idx < state.files.size(); file_idx = state.next_file_idx++) {
			if (file_idx > state.first_fatal_idx.load()) {
				return;
			}
			InferFile(file_idx);
		}
	}

	string TaskType() const override {
		return "XMLFileInferenceTask";
	}

private:
	void InferFile(idx_t file_idx) {
		auto &result = state.results[file_idx];
		try {
			result.inferred = InferFileColumns(state.fs, state.files[file_idx], state.bind_data, state.schema_options,
			                                   state.mode, result.columns);
			return;
		} catch (const OutOfMemoryException &) {
			result.error = std::current_exception();
			result.error_is_fatal = true;
		} catch (const Exception &) {
			result.error = std::current_exception();
			result.error_is_fatal = !state.bind_data.ignore_errors;
		} catch (...) {
			result.error = std::current_exception();
			result.error_is_fatal = true;
		}
		if (result.error_is_fatal) {
			state.MarkFatal(file_idx);
		}
	}

	FileInferenceState &state;
};

// A parsed DOM peaks at 10-14 times its file's size on real datasets and 31 times on markup of one-character
// elements, and libxml2 allocates it outside the buffer manager.
static constexpr idx_t INFERENCE_BYTES_PER_FILE_BYTE = 32;

// A streamed record's DOM takes a few times its bytes, outside the buffer manager, so one record may span at most
// this share of memory_limit.
static constexpr idx_t STREAMED_RECORD_MEMORY_FACTOR = 4;

static idx_t StreamedRecordLimit(ClientContext &context) {
	return BufferManager::GetBufferManager(context).GetMaxMemory() / STREAMED_RECORD_MEMORY_FACTOR;
}

// Files parsed at once during bind-time inference: the xml_inference_threads setting when set,
// otherwise `threads`, lowered so that a DOM per parse at maximum_file_size fits in memory_limit.
static idx_t InferenceTaskCount(ClientContext &context, idx_t file_count, idx_t max_file_size) {
	idx_t limit = NumericCast<idx_t>(TaskScheduler::GetScheduler(context).NumberOfThreads());
	Value setting;
	if (context.TryGetCurrentSetting(XMLReaderFunctions::INFERENCE_THREADS_SETTING, setting) && !setting.IsNull()) {
		limit = setting.GetValue<uint64_t>();
	} else {
		// Divided in two steps so a huge maximum_file_size can't overflow the product
		auto memory = BufferManager::GetBufferManager(context).GetMaxMemory();
		limit = MinValue<idx_t>(limit, memory / INFERENCE_BYTES_PER_FILE_BYTE / MaxValue<idx_t>(max_file_size, 1));
	}
	return MaxValue<idx_t>(MinValue<idx_t>(limit, file_count), 1);
}

// Infer every file's columns on the DuckDB task scheduler, so at most `threads` files, or
// xml_inference_threads when lower, are parsed at once. Results come back in file order, and the
// caller consumes them in that order, so the merged schema and the error a failing bind raises match a
// serial loop over the same files.
static std::vector<FileInferenceResult> InferFilesColumns(ClientContext &context, FileSystem &fs,
                                                          const std::vector<std::string> &files,
                                                          const XMLReadFunctionData &bind_data,
                                                          const XMLSchemaOptions &schema_options, ParseMode mode) {
	FileInferenceState state(fs, files, bind_data, schema_options, mode);
	TaskExecutor executor(context);
	auto task_count = InferenceTaskCount(context, files.size(), bind_data.max_file_size);
	for (idx_t task_idx = 0; task_idx < task_count; task_idx++) {
		executor.ScheduleTask(make_uniq<FileInferenceTask>(executor, state));
	}
	executor.WorkOnTasks();
	return std::move(state.results);
}

// Rethrow a file's inference error when it ends the bind. Returns true when the file contributed
// columns to merge.
static bool ResolveFileInference(const FileInferenceResult &file_result) {
	if (file_result.error) {
		if (file_result.error_is_fatal) {
			std::rethrow_exception(file_result.error);
		}
		return false;
	}
	return file_result.inferred;
}

// =============================================================================
// Internal Unified Functions (used by both XML and HTML)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadDocumentObjectsBind(ClientContext &context,
                                                                     TableFunctionBindInput &input,
                                                                     vector<LogicalType> &return_types,
                                                                     vector<CompatName> &names, ParseMode mode) {
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
		// Sort each glob's matches lexicographically for deterministic, filesystem-independent
		// order (matching read_csv). This is the order preserved across threads under multi-file
		// parallelism (issue #72). Explicitly listed paths — each its own single-match pattern —
		// keep the user-provided order.
		vector<string> matched;
		for (const auto &file_info : glob_result) {
			matched.push_back(file_info.path);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &matched_path : matched) {
			result->files.push_back(std::move(matched_path));
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
		} else if (kv.first == "compression") {
			result->compression = kv.second.ToString();
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

	// Shared work list only; per-file cursor state lives in each worker's local state.
	result->files = bind_data.files;

	return std::move(result);
}

unique_ptr<LocalTableFunctionState> XMLReaderFunctions::ReadDocumentInitLocal(ExecutionContext &context,
                                                                              TableFunctionInitInput &input,
                                                                              GlobalTableFunctionState *global_state) {
	// Each worker gets its own cursor; it claims its first file lazily on the first scan call.
	return make_uniq<XMLReadLocalState>();
}

OperatorPartitionData XMLReaderFunctions::ReadDocumentGetPartitionData(ClientContext &context,
                                                                       TableFunctionGetPartitionInput &input) {
	// Tag the just-produced chunk with the batch index computed at chunk finalization. DuckDB
	// only calls this for non-empty chunks, so last_batch_index always refers to a real chunk.
	auto &lstate = input.local_state->Cast<XMLReadLocalState>();
	// chunk_counter has already advanced past the chunk being tagged.
	FileBatchIndex::Validate(lstate.file_index, lstate.chunk_counter - 1, lstate.current_filename);
	return OperatorPartitionData(lstate.last_batch_index);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadDocumentBind(ClientContext &context, TableFunctionBindInput &input,
                                                              vector<LogicalType> &return_types,
                                                              vector<CompatName> &names, ParseMode mode) {
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
		// Sort each glob's matches lexicographically for deterministic, filesystem-independent
		// order (matching read_csv). This is the order preserved across threads under multi-file
		// parallelism (issue #72). Explicitly listed paths — each its own single-match pattern —
		// keep the user-provided order.
		vector<string> matched;
		for (const auto &file_info : glob_result) {
			matched.push_back(file_info.path);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &matched_path : matched) {
			result->files.push_back(std::move(matched_path));
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
	}

	// Handle optional parameters with schema inference defaults
	XMLSchemaOptions schema_options;
	schema_options.max_record_bytes = StreamedRecordLimit(context);
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
		} else if (kv.first == "sample_files") {
			result->schema_sample_files = kv.second.GetValue<int64_t>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
			schema_options.maximum_file_size = result->max_file_size;
		} else if (kv.first == "compression") {
			result->compression = kv.second.ToString();
			schema_options.compression = result->compression;
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
			schema_options.empty_elements = ParseEmptyElementsParameter(kv.second, function_name);
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "sample_size") {
			// #102: how many element values the type sniffer inspects before locking a column's
			// type. <= 0 samples every value (always-correct detection at the cost of a full scan
			// at bind time).
			schema_options.sample_size = kv.second.GetValue<int32_t>();
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
		} else if (kv.first == "nullstr") {
			ParseNullstrParameter(kv.second, function_name, schema_options.null_strings);
		} else if (kv.first == "streaming") {
			schema_options.streaming = kv.second.GetValue<bool>();
		} else if (kv.first == "preserve_whitespace") {
			schema_options.preserve_whitespace = kv.second.GetValue<bool>();
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
				names.push_back(CompatMakeName(CompatIdentifierName(name)));
			}

			if (return_types.empty()) {
				throw BinderException("%s \"columns\" parameter needs at least one column.", function_name);
			}

			// Store explicit schema in function data
			result->has_explicit_schema = true;
			result->column_names = CompatNamesToStrings(names);
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
			if (result->union_by_name || result->schema_sample_files < 0) {
				// union_by_name (or sample_files := -1): sample every file for a full union schema
				files_to_scan = result->files;
			} else {
				// Sample a bounded number of files (default schema_sample_files) so the inferred
				// schema reflects columns/types beyond the first file, without opening the whole glob.
				idx_t limit = result->schema_sample_files < 1 ? 1 : static_cast<idx_t>(result->schema_sample_files);
				idx_t n = MinValue<idx_t>(result->files.size(), limit);
				for (idx_t i = 0; i < n; i++) {
					files_to_scan.push_back(result->files[i]);
				}
			}

			// Map to track all unique columns and their types across files
			std::unordered_map<std::string, LogicalType> union_schema;
			std::unordered_map<std::string, std::string> union_formats; // Per-column winning datetime format
			std::vector<std::string> column_order;                      // Track order of first appearance

			auto file_results = InferFilesColumns(context, fs, files_to_scan, *result, schema_options, mode);
			for (auto &file_result : file_results) {
				if (!ResolveFileInference(file_result)) {
					continue;
				}
				// Store full inferred schema from first file (preserves is_attribute flags)
				if (result->inferred_schema.empty()) {
					result->inferred_schema = file_result.columns;
					XMLSchemaInference::ResolveUnknownTypes(result->inferred_schema);
				}
				MergeInferredColumns(file_result.columns, union_schema, union_formats, column_order,
				                     schema_options.text_key);
			}

			// Convert union schema to return types
			if (!union_schema.empty()) {
				for (const auto &col_name : column_order) {
					names.push_back(CompatMakeName(col_name));
					return_types.push_back(XMLSchemaInference::ResolveUnknownTypes(union_schema[col_name]));
				}

				// Always store schema for execution to ensure consistent column ordering
				// across multiple files (fixes non-determinism from unordered_map iteration)
				result->has_explicit_schema = true;
				result->column_names = CompatNamesToStrings(names);
				result->column_types = return_types;
				// Store per-column datetime formats for use during extraction
				result->column_datetime_formats.resize(names.size());
				for (size_t i = 0; i < names.size(); i++) {
					auto fmt_it = union_formats.find(CompatIdentifierName(names[i]));
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

		} catch (const OutOfMemoryException &) {
			throw;
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
	auto &lstate = data_p.local_state->Cast<XMLReadLocalState>();

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	const bool is_html = (bind_data.parse_mode == ParseMode::HTML);

	// _objects yields exactly one row (the whole document) per file. Under multi-file
	// parallelism each worker claims one file, emits its single row as one chunk, and tags it
	// with the file's batch index so output stays in glob order.
	while (output_idx < STANDARD_VECTOR_SIZE) {
		if (!lstate.have_file) {
			idx_t claimed = gstate.ClaimNextFile();
			if (claimed == DConstants::INVALID_INDEX) {
				break; // no more work for this worker
			}
			lstate.file_index = claimed;
			lstate.current_filename = gstate.files[claimed];
			lstate.chunk_counter = 0;
			lstate.have_file = true;
		}
		const auto &filename = lstate.current_filename;
		bool emitted = false;

		try {
			string content;
			if (!ReadXMLFile(*OpenXMLFile(fs, filename, bind_data.compression), bind_data.max_file_size, content)) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				// Skip this file (fall through to claim the next one)
			} else {
				bool valid = true;
				// Validate content based on mode
				if (is_html) {
					// HTML mode: be lenient, allow empty files
					if (content.empty()) {
						if (bind_data.ignore_errors) {
							valid = false; // Skip empty file
						} else {
							content = "<html></html>"; // Minimal valid HTML
						}
					}
				} else {
					// XML mode: strict validation
					auto validity = XMLUtils::CheckXML(content);
					if (validity == XMLValidity::ResourceError) {
						ThrowLibxmlOutOfMemory(filename);
					}
					if (validity != XMLValidity::Valid) {
						if (bind_data.ignore_errors) {
							valid = false; // Skip this invalid file
						} else {
							throw InvalidInputException("File %s contains invalid XML", filename);
						}
					}
				}

				if (valid) {
					// Set output values based on schema
					idx_t col_idx = 0;
					if (bind_data.include_filename) {
						output.data[col_idx++].SetValue(output_idx, Value(filename));
					}
					output.data[col_idx++].SetValue(output_idx, Value(content));
					output_idx++;
					emitted = true;
				}
			}
		} catch (const OutOfMemoryException &) {
			throw;
		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue
		}

		// This file is fully handled; claim the next on the following call.
		lstate.have_file = false;
		if (emitted) {
			lstate.last_batch_index = (lstate.file_index << XMLReadLocalState::FILE_SHIFT) | lstate.chunk_counter;
			lstate.chunk_counter++;
			break; // one file (one row) per chunk keeps batch indices ordered
		}
		// Skipped file produced no row: keep claiming without returning an empty chunk.
	}

	CompatSetOutputCardinality(output, output_idx);
}

void XMLReaderFunctions::ReadDocumentFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();
	auto &lstate = data_p.local_state->Cast<XMLReadLocalState>();

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;
	const bool is_html = (bind_data.parse_mode == ParseMode::HTML);
	const auto &schema_options = bind_data.schema_options;

	while (output_idx < STANDARD_VECTOR_SIZE) {
		// Ensure this worker owns a file; claim the next one otherwise.
		if (!lstate.have_file) {
			idx_t claimed = gstate.ClaimNextFile();
			if (claimed == DConstants::INVALID_INDEX) {
				break; // no more work for this worker
			}
			lstate.file_index = claimed;
			lstate.current_filename = gstate.files[claimed];
			lstate.chunk_counter = 0;
			lstate.have_file = true;
			lstate.file_loaded = false;
		}

		// Per-file value source: the file THIS worker claimed, never the global cursor.
		const auto &filename = lstate.current_filename;
		bool file_done = false;

		try {
			// Load file if not already loaded
			if (!lstate.file_loaded) {
				auto file_handle = OpenXMLFile(fs, filename, bind_data.compression);
				bool parse_from_handle = ParsesFromHandle(*file_handle);
				string content;
				bool fits = parse_from_handle ? file_handle->GetFileSize() <= bind_data.max_file_size
				                              : ReadXMLFile(*file_handle, bind_data.max_file_size, content);

				// A file over the size limit streams through SAX when the read allows it
				bool use_sax = !fits && schema_options.streaming && !is_html &&
				               !HasComplexXPath(schema_options.record_element) &&
				               !SchemaHasUnsupportedSAXType(bind_data.column_types);

				if (!fits && !use_sax) {
					if (!bind_data.ignore_errors) {
						throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
						                            bind_data.max_file_size);
					}
					// Skip this file: release and claim the next one without emitting a chunk.
					lstate.ResetFileResources();
					lstate.have_file = false;
					continue;
				}

				lstate.use_sax = use_sax;

				// Remaining depth for extraction against the inferred schema
				int effective_depth = schema_options.max_depth;
				if (effective_depth > 20 || effective_depth < 0) {
					effective_depth = 20;
				}
				lstate.remaining_depth = effective_depth - 2;

				if (use_sax) {
					// Measuring a non-seekable input consumed its first bytes, so the stream starts from them
					lstate.sax_stream = make_uniq<XMLRecordStream>(std::move(file_handle), filename, schema_options, 0,
					                                               false, std::move(content));
					lstate.sax_pending.clear();
					lstate.sax_pending_chunk = 0;
					lstate.sax_pending_pos = 0;
					lstate.sax_parse_complete = false;
					lstate.file_loaded = true;
				} else {
					// This single parse is also the validity check: a separate pre-validation
					// pass would parse the document twice and could not see why a parse failed.
					lstate.current_doc =
					    parse_from_handle ? ParseXMLFile(*file_handle, is_html) : XMLDocRAII(content, is_html);

					if (!lstate.current_doc.IsValid()) {
						if (lstate.current_doc.HadResourceError()) {
							ThrowLibxmlOutOfMemory(filename);
						}
						if (bind_data.ignore_errors) {
							lstate.ResetFileResources();
							lstate.have_file = false;
							continue;
						}
						if (is_html) {
							throw InvalidInputException("Failed to parse file %s", filename);
						}
						throw InvalidInputException("File %s contains invalid XML", filename);
					}

					XMLSchemaInference::ApplyXsiNil(lstate.current_doc.doc);
					xmlNodePtr root = xmlDocGetRootElement(lstate.current_doc.doc);
					if (!root) {
						if (bind_data.ignore_errors) {
							lstate.ResetFileResources();
							lstate.have_file = false;
							continue;
						}
						throw InvalidInputException("File %s has no root element", filename);
					}

					// Find record elements in the DOM
					lstate.record_elements =
					    XMLSchemaInference::IdentifyRecordElements(lstate.current_doc, root, schema_options);

					lstate.current_record_index = 0;
					lstate.file_loaded = true;
				}
			}

			// Columns the record does not provide are NULL so no vector slot is left uninitialized
			// (e.g. fallback schemas where extraction yields fewer values).
			auto write_record = [&](xmlNodePtr record, DataChunk &chunk, idx_t row) {
				idx_t col = 0;
				if (bind_data.include_filename) {
					chunk.data[col++].SetValue(row, Value(filename));
				}
				if (bind_data.has_explicit_schema) {
					col = XMLSchemaInference::WriteRecordWithSchema(record, bind_data.column_names,
					                                                bind_data.column_types, schema_options,
					                                                bind_data.column_datetime_formats, chunk, col, row);
				} else {
					col = XMLSchemaInference::WriteRecord(record, bind_data.inferred_schema, lstate.remaining_depth,
					                                      schema_options, chunk, col, row);
				}
				for (; col < chunk.ColumnCount(); col++) {
					FlatVector::SetNull(chunk.data[col], row, true);
				}
			};

			if (lstate.use_sax) {
				auto &pending = lstate.sax_pending;
				auto emit_pending_sax_rows = [&]() {
					while (lstate.sax_pending_chunk < pending.size() && output_idx < STANDARD_VECTOR_SIZE) {
						auto &chunk = *pending[lstate.sax_pending_chunk];
						CompatSetOutputCardinality(chunk, chunk.size());
						auto count =
						    MinValue<idx_t>(chunk.size() - lstate.sax_pending_pos, STANDARD_VECTOR_SIZE - output_idx);
						for (idx_t col = 0; col < chunk.ColumnCount(); col++) {
							VectorOperations::Copy(chunk.data[col], output.data[col], lstate.sax_pending_pos + count,
							                       lstate.sax_pending_pos, output_idx);
						}
						output_idx += count;
						lstate.sax_pending_pos += count;
						if (lstate.sax_pending_pos == chunk.size()) {
							pending[lstate.sax_pending_chunk++].reset();
							lstate.sax_pending_pos = 0;
						}
					}
					if (lstate.sax_pending_chunk == pending.size()) {
						pending.clear();
						lstate.sax_pending_chunk = 0;
					}
				};

				if (!lstate.sax_parse_complete) {
					while (true) {
						// Fail-closed (ignore_errors=false): emit as records complete. A later parse
						// error aborts the query, so prefix rows never become a result.
						// ignore_errors: withhold until the whole file is well-formed so a truncated
						// or trailing-garbage file matches DOM (skip the file, no prefix rows).
						if (!bind_data.ignore_errors) {
							emit_pending_sax_rows();
							if (output_idx >= STANDARD_VECTOR_SIZE) {
								break;
							}
						}

						bool more = lstate.sax_stream->ParseChunk();
						for (auto record : lstate.sax_stream->Records()) {
							if (pending.empty() && !bind_data.ignore_errors && output_idx < STANDARD_VECTOR_SIZE) {
								write_record(record, output, output_idx++);
								continue;
							}
							if (pending.empty() || pending.back()->size() == STANDARD_VECTOR_SIZE) {
								pending.push_back(make_uniq<DataChunk>());
								pending.back()->Initialize(Allocator::Get(context), output.GetTypes());
							}
							auto &chunk = *pending.back();
							write_record(record, chunk, chunk.size());
							chunk.SetCardinality(chunk.size() + 1);
						}
						lstate.sax_stream->ReleaseRecords();
						if (!more) {
							lstate.sax_parse_complete = true;
							break;
						}
					}
				}

				if (lstate.sax_parse_complete) {
					emit_pending_sax_rows();
					if (pending.empty()) {
						file_done = true;
					}
				}
			} else {
				// DOM extraction: extract records one at a time
				while (lstate.current_record_index < lstate.record_elements.size() &&
				       output_idx < STANDARD_VECTOR_SIZE) {
					write_record(lstate.record_elements[lstate.current_record_index], output, output_idx);
					output_idx++;
					lstate.current_record_index++;
				}

				// Check if we've finished all records from this file
				if (lstate.current_record_index >= lstate.record_elements.size()) {
					file_done = true;
				}
			}

		} catch (const OutOfMemoryException &) {
			throw;
		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip the rest of this file and claim the next one.
			lstate.ResetFileResources();
			lstate.have_file = false;
			continue;
		}

		if (file_done) {
			// File fully consumed. One file (with rows) per output chunk keeps batch indices
			// ordered, so if we produced rows we tag and return this chunk now; an empty file
			// produced nothing, so keep claiming rather than returning a spurious 0-row chunk
			// (which DuckDB reads as end-of-data for this worker).
			lstate.ResetFileResources();
			lstate.have_file = false;
			if (output_idx > 0) {
				lstate.last_batch_index = (lstate.file_index << XMLReadLocalState::FILE_SHIFT) | lstate.chunk_counter;
				lstate.chunk_counter++;
				break;
			}
			continue;
		}

		// File not finished but the output chunk is full: return it; the same file resumes on
		// the next call (its next chunk gets the following within-file batch index).
		if (output_idx >= STANDARD_VECTOR_SIZE) {
			lstate.last_batch_index = (lstate.file_index << XMLReadLocalState::FILE_SHIFT) | lstate.chunk_counter;
			lstate.chunk_counter++;
			break;
		}
	}

	CompatSetOutputCardinality(output, output_idx);
}

// =============================================================================
// Public XML Functions (will be refactored to delegate in Phase 3)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                vector<LogicalType> &return_types,
                                                                vector<CompatName> &names) {
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
		// Sort each glob's matches lexicographically for deterministic, filesystem-independent
		// order (matching read_csv). This is the order preserved across threads under multi-file
		// parallelism (issue #72). Explicitly listed paths — each its own single-match pattern —
		// keep the user-provided order.
		vector<string> matched;
		for (const auto &file_info : glob_result) {
			matched.push_back(file_info.path);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &matched_path : matched) {
			result->files.push_back(std::move(matched_path));
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
		} else if (kv.first == "compression") {
			result->compression = kv.second.ToString();
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

	// Shared work list only; per-file cursor state lives in each worker's local state.
	result->files = bind_data.files;

	return std::move(result);
}

void XMLReaderFunctions::ReadXMLObjectsFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
	auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();
	auto &lstate = data_p.local_state->Cast<XMLReadLocalState>();

	auto &fs = FileSystem::GetFileSystem(context);
	idx_t output_idx = 0;

	// One row (the whole document) per file. Under multi-file parallelism each worker claims
	// one file, emits its single row as one chunk, and tags it with the file's batch index so
	// output stays in glob order.
	while (output_idx < STANDARD_VECTOR_SIZE) {
		if (!lstate.have_file) {
			idx_t claimed = gstate.ClaimNextFile();
			if (claimed == DConstants::INVALID_INDEX) {
				break; // no more work for this worker
			}
			lstate.file_index = claimed;
			lstate.current_filename = gstate.files[claimed];
			lstate.chunk_counter = 0;
			lstate.have_file = true;
		}
		const auto &filename = lstate.current_filename;
		bool emitted = false;

		try {
			string content;
			if (!ReadXMLFile(*OpenXMLFile(fs, filename, bind_data.compression), bind_data.max_file_size, content)) {
				if (!bind_data.ignore_errors) {
					throw InvalidInputException("File %s exceeds maximum size limit (%llu bytes)", filename,
					                            bind_data.max_file_size);
				}
				// Skip this file (fall through to claim the next)
			} else {
				// Validate XML
				auto validity = XMLUtils::CheckXML(content);
				if (validity == XMLValidity::ResourceError) {
					ThrowLibxmlOutOfMemory(filename);
				}
				if (validity != XMLValidity::Valid) {
					if (!bind_data.ignore_errors) {
						throw InvalidInputException("File %s contains invalid XML", filename);
					}
					// Skip this invalid file
				} else {
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
					emitted = true;
				}
			}
		} catch (const OutOfMemoryException &) {
			throw;
		} catch (const Exception &e) {
			if (!bind_data.ignore_errors) {
				throw;
			}
			// Skip this file and continue
		}

		lstate.have_file = false;
		if (emitted) {
			lstate.last_batch_index = (lstate.file_index << XMLReadLocalState::FILE_SHIFT) | lstate.chunk_counter;
			lstate.chunk_counter++;
			break; // one file (one row) per chunk keeps batch indices ordered
		}
		// Skipped file produced no row: keep claiming without returning an empty chunk.
	}

	CompatSetOutputCardinality(output, output_idx);
}

unique_ptr<FunctionData> XMLReaderFunctions::ReadXMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                         vector<LogicalType> &return_types, vector<CompatName> &names) {
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
		// Sort each glob's matches lexicographically for deterministic, filesystem-independent
		// order (matching read_csv). This is the order preserved across threads under multi-file
		// parallelism (issue #72). Explicitly listed paths — each its own single-match pattern —
		// keep the user-provided order.
		vector<string> matched;
		for (const auto &file_info : glob_result) {
			matched.push_back(file_info.path);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &matched_path : matched) {
			result->files.push_back(std::move(matched_path));
		}
	}

	if (result->files.empty()) {
		string pattern_str = file_patterns.size() == 1 ? file_patterns[0] : "provided patterns";
		throw InvalidInputException("No files found matching pattern: %s", pattern_str);
	}

	// Handle optional parameters with schema inference defaults
	XMLSchemaOptions schema_options;
	schema_options.max_record_bytes = StreamedRecordLimit(context);
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
		} else if (kv.first == "sample_files") {
			result->schema_sample_files = kv.second.GetValue<int64_t>();
		} else if (kv.first == "maximum_file_size") {
			result->max_file_size = kv.second.GetValue<idx_t>();
			schema_options.maximum_file_size = result->max_file_size;
		} else if (kv.first == "compression") {
			result->compression = kv.second.ToString();
			schema_options.compression = result->compression;
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
			schema_options.empty_elements = ParseEmptyElementsParameter(kv.second, "read_xml");
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "sample_size") {
			// #102: how many element values the type sniffer inspects before locking a column's
			// type. <= 0 samples every value (always-correct detection at the cost of a full scan
			// at bind time).
			schema_options.sample_size = kv.second.GetValue<int32_t>();
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
		} else if (kv.first == "nullstr") {
			ParseNullstrParameter(kv.second, "read_xml", schema_options.null_strings);
		} else if (kv.first == "streaming") {
			schema_options.streaming = kv.second.GetValue<bool>();
		} else if (kv.first == "preserve_whitespace") {
			schema_options.preserve_whitespace = kv.second.GetValue<bool>();
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
				names.push_back(CompatMakeName(CompatIdentifierName(name)));
			}

			if (return_types.empty()) {
				throw BinderException("read_xml \"columns\" parameter needs at least one column.");
			}

			// Store explicit schema in function data
			result->has_explicit_schema = true;
			result->column_names = CompatNamesToStrings(names);
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
			if (result->union_by_name || result->schema_sample_files < 0) {
				// union_by_name (or sample_files := -1): sample every file for a full union schema
				files_to_scan = result->files;
			} else {
				// Sample a bounded number of files (default schema_sample_files) so the inferred
				// schema reflects columns/types beyond the first file, without opening the whole glob.
				idx_t limit = result->schema_sample_files < 1 ? 1 : static_cast<idx_t>(result->schema_sample_files);
				idx_t n = MinValue<idx_t>(result->files.size(), limit);
				for (idx_t i = 0; i < n; i++) {
					files_to_scan.push_back(result->files[i]);
				}
			}

			// Map to track all unique columns and their types across files
			std::unordered_map<std::string, LogicalType> union_schema;
			std::unordered_map<std::string, std::string> union_formats; // Per-column winning datetime format
			std::vector<std::string> column_order;                      // Track order of first appearance

			auto file_results = InferFilesColumns(context, fs, files_to_scan, *result, schema_options, ParseMode::XML);
			for (auto &file_result : file_results) {
				if (ResolveFileInference(file_result)) {
					MergeInferredColumns(file_result.columns, union_schema, union_formats, column_order,
					                     schema_options.text_key);
				}
			}

			// Convert union schema to return types
			if (!union_schema.empty()) {
				for (const auto &col_name : column_order) {
					names.push_back(CompatMakeName(col_name));
					return_types.push_back(XMLSchemaInference::ResolveUnknownTypes(union_schema[col_name]));
				}

				// Always store schema for execution to ensure consistent column ordering
				// across multiple files (fixes non-determinism from unordered_map iteration)
				result->has_explicit_schema = true;
				result->column_names = CompatNamesToStrings(names);
				result->column_types = return_types;
				// Store per-column datetime formats for use during extraction
				result->column_datetime_formats.resize(names.size());
				for (size_t i = 0; i < names.size(); i++) {
					auto fmt_it = union_formats.find(CompatIdentifierName(names[i]));
					result->column_datetime_formats[i] = (fmt_it != union_formats.end()) ? fmt_it->second : "";
				}
			} else {
				// Fallback to simple schema if no columns were inferred
				return_types.push_back(XMLTypes::XMLType());
				names.push_back("xml");
			}

		} catch (const OutOfMemoryException &) {
			throw;
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
	return ReadDocumentFunction(context, data_p, output);
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
	children.push_back(CompatConstant(Value(table_name)));
	table_function->function = make_uniq<FunctionExpression>("read_xml", std::move(children));

	// Set alias for non-glob patterns
	if (!FileSystem::HasGlob(table_name)) {
		auto &fs = FileSystem::GetFileSystem(context);
		table_function->alias = CompatMakeAlias(fs.ExtractBaseName(table_name));
	}

	return std::move(table_function);
}

// =============================================================================
// Public HTML Functions (delegate to internal unified functions)
// =============================================================================

unique_ptr<FunctionData> XMLReaderFunctions::ReadHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                 vector<LogicalType> &return_types,
                                                                 vector<CompatName> &names) {
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
                                                          vector<LogicalType> &return_types,
                                                          vector<CompatName> &names) {
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
                                                                      vector<CompatName> &names, ParseMode mode) {
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
				CompatSetOutputCardinality(output, 0);
				return;
			}
			// Return minimal valid HTML for empty input
			output.data[0].SetValue(0, Value("<html></html>"));
			CompatSetOutputCardinality(output, 1);
			gstate.current_row = 1;
			return;
		}
	} else {
		// XML mode: strict validation
		if (!XMLUtils::IsValidXML(content)) {
			if (bind_data.ignore_errors) {
				CompatSetOutputCardinality(output, 0);
				return;
			}
			throw InvalidInputException("Input contains invalid XML");
		}
	}

	// Return the content
	output.data[0].SetValue(0, Value(content));
	CompatSetOutputCardinality(output, 1);
	gstate.current_row = 1;
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseDocumentBind(ClientContext &context, TableFunctionBindInput &input,
                                                               vector<LogicalType> &return_types,
                                                               vector<CompatName> &names, ParseMode mode) {
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
			schema_options.empty_elements = ParseEmptyElementsParameter(kv.second, function_name);
		} else if (kv.first == "auto_detect") {
			schema_options.auto_detect = kv.second.GetValue<bool>();
		} else if (kv.first == "max_depth") {
			schema_options.max_depth = kv.second.GetValue<int32_t>();
		} else if (kv.first == "sample_size") {
			// #102: how many element values the type sniffer inspects before locking a column's
			// type. <= 0 samples every value (always-correct detection at the cost of a full scan
			// at bind time).
			schema_options.sample_size = kv.second.GetValue<int32_t>();
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
		} else if (kv.first == "nullstr") {
			ParseNullstrParameter(kv.second, function_name, schema_options.null_strings);
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
				names.push_back(CompatMakeName(CompatIdentifierName(name)));
			}

			if (return_types.empty()) {
				throw BinderException("%s \"columns\" parameter needs at least one column.", function_name);
			}

			result->has_explicit_schema = true;
			result->column_names = CompatNamesToStrings(names);
			result->column_types = return_types;
			has_explicit_columns = true;
		}
	}

	// Check for conflicting parameters
	if (has_explicit_columns && schema_options.all_varchar) {
		throw BinderException("%s cannot use both \"columns\" parameter and \"all_varchar\" option.", function_name);
	}

	result->schema_options = schema_options;

	// Validate XML before either schema path, as read_xml does per file. Extraction against
	// explicit columns returns an empty result for a document that fails to parse, so the
	// check has to run here for malformed input to raise. HTML is left to libxml2's lenient
	// parser.
	bool malformed_xml = false;
	if (mode == ParseMode::XML && !XMLUtils::IsValidXML(result->xml_content)) {
		if (!result->ignore_errors) {
			throw InvalidInputException("Input contains invalid XML");
		}
		malformed_xml = true;
	}

	// Perform schema inference only if no explicit columns were provided
	if (!has_explicit_columns) {
		if (malformed_xml) {
			return_types.push_back(XMLTypes::XMLType());
			names.push_back("xml");
			return std::move(result);
		}
		try {
			const string &content = result->xml_content;

			// Perform schema inference
			auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);

			if (!inferred_columns.empty()) {
				for (const auto &col_info : inferred_columns) {
					names.push_back(CompatMakeName(col_info.name));
					return_types.push_back(col_info.type);
				}

				result->has_explicit_schema = true;
				result->column_names = CompatNamesToStrings(names);
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

		for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
			// NULL-fill columns the row did not provide so no vector slot is left uninitialized
			Value value = col_idx < row.size() ? row[col_idx] : Value(output.data[col_idx].GetType());
			output.data[col_idx].SetValue(output_idx, value);
		}

		output_idx++;
		gstate.current_row++;
	}

	CompatSetOutputCardinality(output, output_idx);
}

// Public parse_xml functions (delegate to internal functions)
unique_ptr<FunctionData> XMLReaderFunctions::ParseXMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                 vector<LogicalType> &return_types,
                                                                 vector<CompatName> &names) {
	return ParseDocumentObjectsBind(context, input, return_types, names, ParseMode::XML);
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseXMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                          vector<LogicalType> &return_types,
                                                          vector<CompatName> &names) {
	return ParseDocumentBind(context, input, return_types, names, ParseMode::XML);
}

// Public parse_html functions (delegate to internal functions)
unique_ptr<FunctionData> XMLReaderFunctions::ParseHTMLObjectsBind(ClientContext &context, TableFunctionBindInput &input,
                                                                  vector<LogicalType> &return_types,
                                                                  vector<CompatName> &names) {
	return ParseDocumentObjectsBind(context, input, return_types, names, ParseMode::HTML);
}

unique_ptr<FunctionData> XMLReaderFunctions::ParseHTMLBind(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types,
                                                           vector<CompatName> &names) {
	return ParseDocumentBind(context, input, return_types, names, ParseMode::HTML);
}

constexpr const char *XMLReaderFunctions::INFERENCE_THREADS_SETTING;

static void CheckInferenceThreads(ClientContext &context, SetScope scope, Value &parameter) {
	if (!parameter.IsNull() && parameter.GetValue<uint64_t>() == 0) {
		throw InvalidInputException("%s must be at least 1", XMLReaderFunctions::INFERENCE_THREADS_SETTING);
	}
}

void XMLReaderFunctions::Register(ExtensionLoader &loader) {
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption(
	    INFERENCE_THREADS_SETTING,
	    "Maximum number of files read_xml and read_html parse at once while inferring a "
	    "schema at bind. Unset uses the threads setting, lowered to fit memory_limit at maximum_file_size.",
	    LogicalType::UBIGINT, Value(), CheckInferenceThreads);

	// Register read_xml_objects table function (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_xml_objects_set("read_xml_objects");

	// Variant 1: Single string parameter
	TableFunction read_xml_objects_single("read_xml_objects", {LogicalType::VARCHAR}, ReadXMLObjectsFunction,
	                                      ReadXMLObjectsBind, ReadXMLObjectsInit);
	DeclareNamedParameters(read_xml_objects_single, {
	                                                    {"ignore_errors", LogicalType::BOOLEAN},
	                                                    {"maximum_file_size", LogicalType::BIGINT},
	                                                    {"compression", LogicalType::VARCHAR},
	                                                    {"filename", LogicalType::BOOLEAN},
	                                                });
	read_xml_objects_single.init_local = ReadDocumentInitLocal;
	read_xml_objects_single.get_partition_data = ReadDocumentGetPartitionData;
	read_xml_objects_set.AddFunction(read_xml_objects_single);

	// Variant 2: Array of strings parameter
	TableFunction read_xml_objects_array("read_xml_objects", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                     ReadXMLObjectsFunction, ReadXMLObjectsBind, ReadXMLObjectsInit);
	DeclareNamedParameters(read_xml_objects_array, {
	                                                   {"ignore_errors", LogicalType::BOOLEAN},
	                                                   {"maximum_file_size", LogicalType::BIGINT},
	                                                   {"compression", LogicalType::VARCHAR},
	                                                   {"filename", LogicalType::BOOLEAN},
	                                               });
	read_xml_objects_array.init_local = ReadDocumentInitLocal;
	read_xml_objects_array.get_partition_data = ReadDocumentGetPartitionData;
	read_xml_objects_set.AddFunction(read_xml_objects_array);

	{
		CreateTableFunctionInfo info(std::move(read_xml_objects_set));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"file_path"};
		desc.description = "Read an XML file and return raw XML object content.";
		desc.examples = {"SELECT * FROM read_xml_objects('data.xml')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// Register read_xml table function with schema inference (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_xml_set("read_xml");

	// Variant 1: Single string parameter
	TableFunction read_xml_single("read_xml", {LogicalType::VARCHAR}, ReadXMLFunction, ReadXMLBind, ReadXMLInit);
	DeclareNamedParameters(read_xml_single, {
	                                            {"ignore_errors", LogicalType::BOOLEAN},
	                                            {"maximum_file_size", LogicalType::BIGINT},
	                                            {"compression", LogicalType::VARCHAR},
	                                            {"union_by_name", LogicalType::BOOLEAN},
	                                            {"filename", LogicalType::BOOLEAN},
	                                        });
	// Schema inference parameters
	ExtendNamedParameters(
	    read_xml_single,
	    {
	        {"root_element", LogicalType::VARCHAR},
	        {"attr_mode", LogicalType::VARCHAR},      // 'columns' | 'prefixed' | 'map' | 'discard'
	        {"attr_prefix", LogicalType::VARCHAR},    // Prefix for attributes when attr_mode='prefixed'
	        {"text_key", LogicalType::VARCHAR},       // Key for mixed text content
	        {"namespaces", LogicalType::VARCHAR},     // 'strip' | 'expand' | 'keep'
	        {"empty_elements", LogicalType::VARCHAR}, // 'null' | 'string' | 'object'
	        {"auto_detect", LogicalType::BOOLEAN},
	        {"max_depth", LogicalType::INTEGER},
	        {"sample_size", LogicalType::INTEGER},
	        {"sample_files", LogicalType::BIGINT},
	        {"unnest_as", LogicalType::VARCHAR},      // 'columns' (default) or 'struct' (future)
	        {"record_element", LogicalType::VARCHAR}, // XPath or tag name for elements that should be rows
	        {"force_list", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	    });
	// Explicit schema specification (like JSON extension)
	ExtendNamedParameters(read_xml_single, {
	                                           {"columns", LogicalType::ANY},
	                                           {"all_varchar", LogicalType::BOOLEAN},
	                                           {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                           {"nullstr", LogicalType::ANY},
	                                           {"streaming", LogicalType::BOOLEAN},
	                                           {"preserve_whitespace", LogicalType::BOOLEAN},
	                                       });
	read_xml_single.init_local = ReadDocumentInitLocal;
	read_xml_single.get_partition_data = ReadDocumentGetPartitionData;
	read_xml_set.AddFunction(read_xml_single);

	// Variant 2: Array of strings parameter
	TableFunction read_xml_array("read_xml", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadXMLFunction, ReadXMLBind,
	                             ReadXMLInit);
	DeclareNamedParameters(read_xml_array, {
	                                           {"ignore_errors", LogicalType::BOOLEAN},
	                                           {"maximum_file_size", LogicalType::BIGINT},
	                                           {"compression", LogicalType::VARCHAR},
	                                           {"union_by_name", LogicalType::BOOLEAN},
	                                           {"filename", LogicalType::BOOLEAN},
	                                       });
	// Schema inference parameters
	ExtendNamedParameters(
	    read_xml_array,
	    {
	        {"root_element", LogicalType::VARCHAR},
	        {"attr_mode", LogicalType::VARCHAR},      // 'columns' | 'prefixed' | 'map' | 'discard'
	        {"attr_prefix", LogicalType::VARCHAR},    // Prefix for attributes when attr_mode='prefixed'
	        {"text_key", LogicalType::VARCHAR},       // Key for mixed text content
	        {"namespaces", LogicalType::VARCHAR},     // 'strip' | 'expand' | 'keep'
	        {"empty_elements", LogicalType::VARCHAR}, // 'null' | 'string' | 'object'
	        {"auto_detect", LogicalType::BOOLEAN},
	        {"max_depth", LogicalType::INTEGER},
	        {"sample_size", LogicalType::INTEGER},
	        {"sample_files", LogicalType::BIGINT},
	        {"unnest_as", LogicalType::VARCHAR},      // 'columns' (default) or 'struct' (future)
	        {"record_element", LogicalType::VARCHAR}, // XPath or tag name for elements that should be rows
	        {"force_list", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	    });
	// Explicit schema specification (like JSON extension)
	ExtendNamedParameters(read_xml_array, {
	                                          {"columns", LogicalType::ANY},
	                                          {"all_varchar", LogicalType::BOOLEAN},
	                                          {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                          {"nullstr", LogicalType::ANY},
	                                          {"streaming", LogicalType::BOOLEAN},
	                                          {"preserve_whitespace", LogicalType::BOOLEAN},
	                                      });
	read_xml_array.init_local = ReadDocumentInitLocal;
	read_xml_array.get_partition_data = ReadDocumentGetPartitionData;
	read_xml_set.AddFunction(read_xml_array);

	{
		CreateTableFunctionInfo info(std::move(read_xml_set));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"file_path"};
		desc.description = "Read an XML file with automatic schema inference and return tabular data.";
		desc.examples = {"SELECT * FROM read_xml('data.xml')",
		                 "SELECT * FROM read_xml('data.xml', record_element := '//item')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// Register read_html table function for reading HTML files (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_html_set("read_html");

	// Variant 1: Single string parameter
	TableFunction read_html_single("read_html", {LogicalType::VARCHAR}, ReadHTMLFunction, ReadHTMLBind, ReadHTMLInit);
	DeclareNamedParameters(read_html_single, {
	                                             {"ignore_errors", LogicalType::BOOLEAN},
	                                             {"maximum_file_size", LogicalType::BIGINT},
	                                             {"compression", LogicalType::VARCHAR},
	                                             {"union_by_name", LogicalType::BOOLEAN},
	                                             {"filename", LogicalType::BOOLEAN},
	                                         });
	// Schema inference parameters (same as read_xml for API consistency)
	ExtendNamedParameters(
	    read_html_single,
	    {
	        {"root_element", LogicalType::VARCHAR},
	        {"attr_mode", LogicalType::VARCHAR},      // 'columns' | 'prefixed' | 'map' | 'discard'
	        {"attr_prefix", LogicalType::VARCHAR},    // Prefix for attributes when attr_mode='prefixed'
	        {"text_key", LogicalType::VARCHAR},       // Key for mixed text content
	        {"namespaces", LogicalType::VARCHAR},     // 'strip' | 'expand' | 'keep'
	        {"empty_elements", LogicalType::VARCHAR}, // 'null' | 'string' | 'object'
	        {"auto_detect", LogicalType::BOOLEAN},
	        {"max_depth", LogicalType::INTEGER},
	        {"sample_size", LogicalType::INTEGER},
	        {"sample_files", LogicalType::BIGINT},
	        {"unnest_as", LogicalType::VARCHAR},      // 'columns' (default) or 'struct' (future)
	        {"record_element", LogicalType::VARCHAR}, // XPath or tag name for elements that should be rows
	        {"force_list", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	    });
	// Explicit schema specification (like JSON extension)
	ExtendNamedParameters(read_html_single, {
	                                            {"columns", LogicalType::ANY},
	                                            {"all_varchar", LogicalType::BOOLEAN},
	                                            {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                            {"nullstr", LogicalType::ANY},
	                                            {"preserve_whitespace", LogicalType::BOOLEAN},
	                                        });
	read_html_single.init_local = ReadDocumentInitLocal;
	read_html_single.get_partition_data = ReadDocumentGetPartitionData;
	read_html_set.AddFunction(read_html_single);

	// Variant 2: Array of strings parameter
	TableFunction read_html_array("read_html", {LogicalType::LIST(LogicalType::VARCHAR)}, ReadHTMLFunction,
	                              ReadHTMLBind, ReadHTMLInit);
	DeclareNamedParameters(read_html_array, {
	                                            {"ignore_errors", LogicalType::BOOLEAN},
	                                            {"maximum_file_size", LogicalType::BIGINT},
	                                            {"compression", LogicalType::VARCHAR},
	                                            {"union_by_name", LogicalType::BOOLEAN},
	                                            {"filename", LogicalType::BOOLEAN},
	                                        });
	// Schema inference parameters (same as read_xml for API consistency)
	ExtendNamedParameters(
	    read_html_array,
	    {
	        {"root_element", LogicalType::VARCHAR},
	        {"attr_mode", LogicalType::VARCHAR},      // 'columns' | 'prefixed' | 'map' | 'discard'
	        {"attr_prefix", LogicalType::VARCHAR},    // Prefix for attributes when attr_mode='prefixed'
	        {"text_key", LogicalType::VARCHAR},       // Key for mixed text content
	        {"namespaces", LogicalType::VARCHAR},     // 'strip' | 'expand' | 'keep'
	        {"empty_elements", LogicalType::VARCHAR}, // 'null' | 'string' | 'object'
	        {"auto_detect", LogicalType::BOOLEAN},
	        {"max_depth", LogicalType::INTEGER},
	        {"sample_size", LogicalType::INTEGER},
	        {"sample_files", LogicalType::BIGINT},
	        {"unnest_as", LogicalType::VARCHAR},      // 'columns' (default) or 'struct' (future)
	        {"record_element", LogicalType::VARCHAR}, // XPath or tag name for elements that should be rows
	        {"force_list", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR): element names that should always be LIST type
	    });
	// Explicit schema specification (like JSON extension)
	ExtendNamedParameters(read_html_array, {
	                                           {"columns", LogicalType::ANY},
	                                           {"all_varchar", LogicalType::BOOLEAN},
	                                           {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                           {"nullstr", LogicalType::ANY},
	                                           {"preserve_whitespace", LogicalType::BOOLEAN},
	                                       });
	read_html_array.init_local = ReadDocumentInitLocal;
	read_html_array.get_partition_data = ReadDocumentGetPartitionData;
	read_html_set.AddFunction(read_html_array);

	{
		CreateTableFunctionInfo info(std::move(read_html_set));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"file_path"};
		desc.description = "Read an HTML file with automatic schema inference and return tabular data.";
		desc.examples = {"SELECT * FROM read_html('page.html')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// Register read_html_objects table function for batch HTML processing (supports both VARCHAR and VARCHAR[])
	TableFunctionSet read_html_objects_set("read_html_objects");

	// Variant 1: Single string parameter
	TableFunction read_html_objects_single("read_html_objects", {LogicalType::VARCHAR}, ReadHTMLObjectsFunction,
	                                       ReadHTMLObjectsBind, ReadHTMLObjectsInit);
	DeclareNamedParameters(read_html_objects_single, {
	                                                     {"ignore_errors", LogicalType::BOOLEAN},
	                                                     {"maximum_file_size", LogicalType::BIGINT},
	                                                     {"compression", LogicalType::VARCHAR},
	                                                     {"filename", LogicalType::BOOLEAN},
	                                                 });
	read_html_objects_single.init_local = ReadDocumentInitLocal;
	read_html_objects_single.get_partition_data = ReadDocumentGetPartitionData;
	read_html_objects_set.AddFunction(read_html_objects_single);

	// Variant 2: Array of strings parameter
	TableFunction read_html_objects_array("read_html_objects", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                      ReadHTMLObjectsFunction, ReadHTMLObjectsBind, ReadHTMLObjectsInit);
	DeclareNamedParameters(read_html_objects_array, {
	                                                    {"ignore_errors", LogicalType::BOOLEAN},
	                                                    {"maximum_file_size", LogicalType::BIGINT},
	                                                    {"compression", LogicalType::VARCHAR},
	                                                    {"filename", LogicalType::BOOLEAN},
	                                                });
	read_html_objects_array.init_local = ReadDocumentInitLocal;
	read_html_objects_array.get_partition_data = ReadDocumentGetPartitionData;
	read_html_objects_set.AddFunction(read_html_objects_array);

	{
		CreateTableFunctionInfo info(std::move(read_html_objects_set));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"file_path"};
		desc.description = "Read an HTML file and return raw HTML object content.";
		desc.examples = {"SELECT * FROM read_html_objects('page.html')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// Register html_extract_tables table function
	TableFunction html_extract_tables_function("html_extract_tables", {LogicalType::VARCHAR}, HTMLExtractTablesFunction,
	                                           HTMLExtractTablesBind, HTMLExtractTablesInit);
	{
		CreateTableFunctionInfo info(std::move(html_extract_tables_function));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"html"};
		desc.description = "Extract all HTML tables from an HTML string as structured rows.";
		desc.examples = {"SELECT * FROM html_extract_tables('<table><tr><td>A</td></tr></table>')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// =============================================================================
	// Register parse_xml_objects table function (parses XML string, returns raw content)
	// =============================================================================
	TableFunction parse_xml_objects("parse_xml_objects", {LogicalType::VARCHAR}, ParseDocumentObjectsFunction,
	                                ParseXMLObjectsBind, ParseDocumentObjectsInit);
	DeclareNamedParameters(parse_xml_objects, {
	                                              {"ignore_errors", LogicalType::BOOLEAN},
	                                          });
	{
		CreateTableFunctionInfo info(std::move(parse_xml_objects));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"xml"};
		desc.description = "Parse an XML string and return raw XML object content.";
		desc.examples = {"SELECT * FROM parse_xml_objects('<root><item>1</item></root>')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// =============================================================================
	// Register parse_html_objects table function (parses HTML string, returns raw content)
	// =============================================================================
	TableFunction parse_html_objects("parse_html_objects", {LogicalType::VARCHAR}, ParseDocumentObjectsFunction,
	                                 ParseHTMLObjectsBind, ParseDocumentObjectsInit);
	DeclareNamedParameters(parse_html_objects, {
	                                               {"ignore_errors", LogicalType::BOOLEAN},
	                                           });
	{
		CreateTableFunctionInfo info(std::move(parse_html_objects));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"html"};
		desc.description = "Parse an HTML string and return raw HTML object content.";
		desc.examples = {"SELECT * FROM parse_html_objects('<div><span>A</span></div>')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// =============================================================================
	// Register parse_xml table function (parses XML string with schema inference)
	// =============================================================================
	TableFunction parse_xml("parse_xml", {LogicalType::VARCHAR}, ParseDocumentFunction, ParseXMLBind,
	                        ParseDocumentInit);
	DeclareNamedParameters(parse_xml, {
	                                      {"ignore_errors", LogicalType::BOOLEAN},
	                                  });
	// Schema inference parameters (same as read_xml)
	ExtendNamedParameters(parse_xml, {
	                                     {"root_element", LogicalType::VARCHAR},
	                                     {"record_element", LogicalType::VARCHAR},
	                                     {"force_list", LogicalType::ANY},
	                                     {"attr_mode", LogicalType::VARCHAR},
	                                     {"attr_prefix", LogicalType::VARCHAR},
	                                     {"text_key", LogicalType::VARCHAR},
	                                     {"namespaces", LogicalType::VARCHAR},
	                                     {"empty_elements", LogicalType::VARCHAR},
	                                     {"auto_detect", LogicalType::BOOLEAN},
	                                     {"max_depth", LogicalType::INTEGER},
	                                     {"sample_size", LogicalType::INTEGER},
	                                     {"unnest_as", LogicalType::VARCHAR},
	                                     {"all_varchar", LogicalType::BOOLEAN},
	                                     {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                     {"nullstr", LogicalType::ANY},
	                                     {"columns", LogicalType::ANY},
	                                 });
	{
		CreateTableFunctionInfo info(std::move(parse_xml));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"xml"};
		desc.description = "Parse an XML string with automatic schema inference and return tabular data.";
		desc.examples = {"SELECT * FROM parse_xml('<root><item id=\"1\">A</item></root>')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}

	// =============================================================================
	// Register parse_html table function (parses HTML string with schema inference)
	// =============================================================================
	TableFunction parse_html("parse_html", {LogicalType::VARCHAR}, ParseDocumentFunction, ParseHTMLBind,
	                         ParseDocumentInit);
	DeclareNamedParameters(parse_html, {
	                                       {"ignore_errors", LogicalType::BOOLEAN},
	                                   });
	// Schema inference parameters (same as read_html)
	ExtendNamedParameters(parse_html, {
	                                      {"root_element", LogicalType::VARCHAR},
	                                      {"record_element", LogicalType::VARCHAR},
	                                      {"force_list", LogicalType::ANY},
	                                      {"attr_mode", LogicalType::VARCHAR},
	                                      {"attr_prefix", LogicalType::VARCHAR},
	                                      {"text_key", LogicalType::VARCHAR},
	                                      {"namespaces", LogicalType::VARCHAR},
	                                      {"empty_elements", LogicalType::VARCHAR},
	                                      {"auto_detect", LogicalType::BOOLEAN},
	                                      {"max_depth", LogicalType::INTEGER},
	                                      {"sample_size", LogicalType::INTEGER},
	                                      {"unnest_as", LogicalType::VARCHAR},
	                                      {"all_varchar", LogicalType::BOOLEAN},
	                                      {"datetime_format", LogicalType::ANY}, // VARCHAR or LIST(VARCHAR)
	                                      {"nullstr", LogicalType::ANY},
	                                      {"columns", LogicalType::ANY},
	                                  });
	{
		CreateTableFunctionInfo info(std::move(parse_html));
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		FunctionDescription desc;
		desc.parameter_names = {"html"};
		desc.description = "Parse an HTML string with automatic schema inference and return tabular data.";
		desc.examples = {"SELECT * FROM parse_html('<div><p>Paragraph</p></div>')"};
		desc.categories = {"webbed"};
		info.descriptions.push_back(desc);
		loader.RegisterFunction(std::move(info));
	}
}

unique_ptr<FunctionData> XMLReaderFunctions::HTMLExtractTablesBind(ClientContext &context,
                                                                   TableFunctionBindInput &input,
                                                                   vector<LogicalType> &return_types,
                                                                   vector<CompatName> &names) {
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

	CompatSetOutputCardinality(output, output_idx);
}

} // namespace duckdb
