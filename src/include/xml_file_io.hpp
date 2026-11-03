#pragma once

#include "duckdb/common/file_system.hpp"
#include "duckdb/common/types/value.hpp"
#include "xml_utils.hpp"

namespace duckdb {

//! Expand each file pattern to its matches, sorted within a pattern and kept in list order across patterns. A pattern
//! that matches nothing, a literal path included, raises an IOException naming it, unless
//! skip_missing is set. Then it is skipped.
vector<string> ExpandFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing);

//! Whether a reader's named parameters set ignore_errors. Read before the readers parse their other parameters, since
//! file expansion needs it.
template <class NAMED_PARAMETERS>
bool IgnoreErrorsParameter(const NAMED_PARAMETERS &named_parameters) {
	for (auto &kv : named_parameters) {
		if (kv.first == "ignore_errors") {
			return kv.second.template GetValue<bool>();
		}
	}
	return false;
}

//! The maximum_file_size parameter in bytes. A negative value raises an InvalidInputException.
idx_t MaximumFileSizeParameter(const Value &value);

//! Open an input file. compression is the read_xml parameter, parsed here so an unknown scheme fails when a file
//! opens, as on DuckDB v2.0. "auto" decompresses a name ending in .gz, or .zst once the parquet extension is loaded.
unique_ptr<FileHandle> OpenXMLFile(FileSystem &fs, const string &path, const string &compression);

//! Read a whole file into content. Returns false when the file holds more than max_size bytes. A compressed file
//! only reveals that by decompressing up to the limit, so Reset the handle before reading it again.
bool ReadXMLFile(FileHandle &handle, idx_t max_size, string &content);

//! Whether to parse a file with ParseXMLFile. A small file reads faster whole, and only a large one's bytes are worth
//! freeing.
bool ParsesFromHandle(FileHandle &handle);

//! Parse a file into a DOM as it reads, without holding a copy of its bytes.
XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html);

//! Parse an HTML file into a DOM as it reads, decoding it per its BOM, <meta> charset or bytes. Returns null when the
//! file is empty.
XMLDocPtr ParseHTMLFile(FileHandle &handle);

} // namespace duckdb
