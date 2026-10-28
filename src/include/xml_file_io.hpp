#pragma once

#include "duckdb/common/file_system.hpp"
#include "xml_utils.hpp"

namespace duckdb {

//! Open an input file. AUTO_DETECT decompresses a name ending in .gz, or .zst once the parquet extension is loaded.
unique_ptr<FileHandle> OpenXMLFile(FileSystem &fs, const string &path, FileCompressionType compression);

//! Read a whole file into content. Returns false when the file holds more than max_size bytes. A compressed file
//! only reveals that by decompressing up to the limit, so Reset the handle before reading it again.
bool ReadXMLFile(FileHandle &handle, idx_t max_size, string &content);

//! Whether to parse a file with ParseXMLFile. A small file reads faster whole, and only a large one's bytes are worth
//! freeing.
bool ParsesFromHandle(FileHandle &handle);

//! Parse a file into a DOM as it reads, without holding a copy of its bytes.
XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html);

} // namespace duckdb
