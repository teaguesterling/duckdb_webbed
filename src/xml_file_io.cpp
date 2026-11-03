#include "xml_file_io.hpp"

#include "duckdb/common/file_open_flags.hpp"
#include "duckdb_compat.hpp"
#include "utf8proc_wrapper.hpp"
#include "xml_in_memory_reader.hpp"
#include "xml_utils.hpp"

#include <algorithm>
#include <exception>

namespace duckdb {

vector<string> ExpandFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing) {
	vector<string> files;
	for (const auto &pattern : patterns) {
		// Sorted for an order independent of the filesystem, as read_csv does. Readers keep it across threads.
		vector<string> matched;
		for (const auto &file_info : fs.Glob(pattern, nullptr)) {
			matched.push_back(file_info.path);
		}
		if (matched.empty() && !skip_missing) {
			throw IOException("No files found that match the pattern \"%s\"", pattern);
		}
		std::sort(matched.begin(), matched.end());
		for (auto &path : matched) {
			files.push_back(std::move(path));
		}
	}
	return files;
}

idx_t MaximumFileSizeParameter(const Value &value) {
	auto size = value.GetValue<int64_t>();
	if (size < 0) {
		throw InvalidInputException("maximum_file_size must be non-negative");
	}
	return static_cast<idx_t>(size);
}

unique_ptr<FileHandle> OpenXMLFile(FileSystem &fs, const string &path, const string &compression) {
	return fs.OpenFile(path, FileFlags::FILE_FLAGS_READ | CompatFileCompressionTypeFromString(compression));
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

// Length of the UTF-8 sequence a lead byte starts, or 0 for a continuation byte.
static idx_t Utf8SequenceLength(unsigned char lead) {
	if (lead < 0x80) {
		return 1;
	}
	if (lead < 0xC0) {
		return 0;
	}
	return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
}

// Fills `buffer` from the handle until it holds `size` bytes or the file ends, and returns how many it read.
static idx_t ReadUpTo(FileHandle &handle, char *buffer, idx_t size) {
	idx_t filled = 0;
	while (filled < size) {
		int64_t got = handle.Read(buffer + filled, size - filled);
		if (got < 0) {
			throw IOException("Could not read from file \"%s\"", handle.path);
		}
		if (got == 0) {
			break;
		}
		filled += UnsafeNumericCast<idx_t>(got);
	}
	return filled;
}

// Whether the rest of the handle, after `carry` bytes already in `buffer`, is valid UTF-8. Reads until the first
// invalid sequence.
static bool RestIsValidUtf8(FileHandle &handle, std::string &buffer, idx_t carry) {
	constexpr idx_t CHUNK_SIZE = idx_t(1) << 20;
	while (true) {
		buffer.resize(carry + CHUNK_SIZE);
		idx_t got = ReadUpTo(handle, &buffer[carry], CHUNK_SIZE);
		idx_t filled = carry + got;
		if (got == 0) {
			return filled == 0 || Utf8Proc::Analyze(buffer.data(), filled) != UnicodeType::INVALID;
		}
		// Hold back a sequence the chunk cuts off. The next chunk checks it whole.
		idx_t cut = filled;
		for (idx_t back = 1; back <= 3 && back <= filled; back++) {
			auto len = Utf8SequenceLength(static_cast<unsigned char>(buffer[filled - back]));
			if (len == 0) {
				continue;
			}
			if (len > back) {
				cut = filled - back;
			}
			break;
		}
		if (Utf8Proc::Analyze(buffer.data(), cut) == UnicodeType::INVALID) {
			return false;
		}
		carry = filled - cut;
		buffer.erase(0, cut);
	}
}

// Chooses the file's HTML encoding, then rewinds it for the parse. A BOM or <meta> in the first bytes decides it.
// Otherwise the choice needs to know whether every byte is valid UTF-8, which a parse that streams cannot learn
// before it starts decoding, and the rest of the file is read once to find out.
static std::string DetectHTMLHandleEncoding(FileHandle &handle) {
	std::string buffer(XMLUtils::HTML_ENCODING_SNIFF_BYTES, '\0');
	idx_t head_size = ReadUpTo(handle, &buffer[0], buffer.size());
	auto encoding = XMLUtils::DeclaredHTMLEncoding(buffer.data(), head_size);
	if (encoding.empty()) {
		buffer.resize(head_size);
		encoding = XMLUtils::UndeclaredHTMLEncoding(RestIsValidUtf8(handle, buffer, head_size));
	}
	handle.Reset();
	return encoding;
}

XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html) {
	std::string html_encoding = is_html ? DetectHTMLHandleEncoding(handle) : "UTF-8";
	FileHandleReader reader {handle, nullptr};
	XMLDocRAII doc(FileHandleReaderRead, &reader, is_html, html_encoding);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

XMLDocPtr ParseHTMLFile(FileHandle &handle) {
	std::string encoding = DetectHTMLHandleEncoding(handle);
	FileHandleReader reader {handle, nullptr};
	auto doc = XMLUtils::ParseHTMLDocument(FileHandleReaderRead, &reader, encoding);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

} // namespace duckdb
