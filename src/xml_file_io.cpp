#include "xml_file_io.hpp"

#include "duckdb/common/file_open_flags.hpp"
#include "xml_in_memory_reader.hpp"

#include <exception>

namespace duckdb {

unique_ptr<FileHandle> OpenXMLFile(FileSystem &fs, const string &path, FileCompressionType compression) {
	return fs.OpenFile(path, FileFlags::FILE_FLAGS_READ | compression);
}

// Reads in chunks of at most 1 GiB, because some platforms cap one read at INT_MAX bytes. A short read is legal on
// network handles, so only a non-positive return ends the loop.
static void ReadExactly(FileHandle &handle, char *data, idx_t size) {
	idx_t offset = 0;
	while (offset < size) {
		idx_t want = MinValue<idx_t>(size - offset, idx_t(1) << 30);
		int64_t got = handle.Read(data + offset, want);
		if (got <= 0) {
			throw IOException("Could not read from file \"%s\": unexpected end of file at offset %llu of %llu",
			                  handle.path, offset, size);
		}
		offset += UnsafeNumericCast<idx_t>(got);
	}
}

bool ReadXMLFile(FileHandle &handle, idx_t max_size, string &content) {
	// A seekable handle reports the size of the bytes it reads. A decompressing handle or a pipe doesn't.
	if (handle.CanSeek()) {
		auto size = handle.GetFileSize();
		if (size > max_size) {
			return false;
		}
		content.resize(size);
		ReadExactly(handle, &content[0], size);
		return true;
	}
	constexpr idx_t CHUNK_SIZE = idx_t(1) << 20;
	content.clear();
	while (true) {
		auto offset = content.size();
		auto room = max_size - offset;
		// Ask for one byte past the limit so a file of exactly max_size bytes isn't rejected.
		auto want = room < CHUNK_SIZE ? room + 1 : CHUNK_SIZE;
		content.resize(offset + want);
		int64_t got = handle.Read(&content[offset], want);
		if (got < 0) {
			throw IOException("Could not read from file \"%s\"", handle.path);
		}
		content.resize(offset + UnsafeNumericCast<idx_t>(got));
		if (content.size() > max_size) {
			return false;
		}
		if (got == 0) {
			return true;
		}
	}
}

bool ParsesFromHandle(FileHandle &handle) {
	return handle.CanSeek() && handle.GetFileSize() > (idx_t(16) << 20);
}

struct FileHandleReader {
	FileHandle &handle;
	std::exception_ptr error;
};

// libxml2 calls this from C frames, so a read error is held and rethrown once the parse returns.
static int FileHandleReaderRead(void *context, char *buffer, int len) {
	auto &reader = *static_cast<FileHandleReader *>(context);
	try {
		return NumericCast<int>(reader.handle.Read(buffer, UnsafeNumericCast<idx_t>(len)));
	} catch (...) {
		reader.error = std::current_exception();
		return -1;
	}
}

XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html) {
	FileHandleReader reader {handle, nullptr};
	XMLDocRAII doc(FileHandleReaderRead, &reader, is_html);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

} // namespace duckdb
