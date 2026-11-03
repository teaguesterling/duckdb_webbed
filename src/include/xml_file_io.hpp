#pragma once

#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/types/value.hpp"
#include "xml_utils.hpp"

namespace duckdb {

// A parsed DOM peaks at 10-14 times its file's size on real datasets and 31 times on markup of one-character
// elements, and libxml2 allocates it outside the buffer manager. Readers size their parallelism and refuse files by
// this bound.
static constexpr idx_t DOM_BYTES_PER_FILE_BYTE = 32;

//! How many DOMs of a `file_size`-byte file fit in memory_limit at once, by DOM_BYTES_PER_FILE_BYTE. 0 when not one
//! does. Readers cap the files they parse at once by it.
idx_t DOMsThatFit(ClientContext &context, idx_t file_size);

//! Expand each file pattern to its matches, sorted within a pattern and kept in list order across patterns. A pattern
//! that matches nothing, a literal path included, raises an IOException naming it, unless
//! skip_missing is set. Then it is skipped.
vector<string> ExpandFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing);
//! ExpandFilePatterns, keeping what the glob learned about each file.
vector<OpenFileInfo> GlobFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing);
//! The size a glob reported for a file, when the filesystem reports one.
optional_idx GlobbedFileSize(const OpenFileInfo &file);

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
