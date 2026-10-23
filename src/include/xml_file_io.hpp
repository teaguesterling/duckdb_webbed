#pragma once

#include "duckdb/common/file_system.hpp"

namespace duckdb {

//! Open an input file, decompressing it when its name ends in .gz, or .zst once the parquet extension is loaded.
unique_ptr<FileHandle> OpenXMLFile(FileSystem &fs, const string &path);

//! Read a whole file into content. Returns false when the file holds more than max_size bytes. A compressed file
//! only reveals that by decompressing up to the limit, so Reset the handle before reading it again.
bool ReadXMLFile(FileHandle &handle, idx_t max_size, string &content);

} // namespace duckdb
