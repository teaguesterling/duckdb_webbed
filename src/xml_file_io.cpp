#include "xml_file_io.hpp"

#include "duckdb/common/file_open_flags.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb_compat.hpp"
#include "html_encoding_sniffer.hpp"
#include "xml_in_memory_reader.hpp"
#include "xml_utils.hpp"

#include <algorithm>
#include <cstring>
#include <exception>

namespace duckdb {

idx_t DOMsThatFit(ClientContext &context, idx_t file_size, idx_t bytes_per_file_byte) {
	// Divided in two steps so the product can't overflow
	auto memory = BufferManager::GetBufferManager(context).GetMaxMemory();
	return memory / bytes_per_file_byte / MaxValue<idx_t>(file_size, 1);
}

void DOMCostCounter::Feed(const char *data, idx_t size) {
	char previous = last;
	for (idx_t i = 0; i < size; i++) {
		char c = data[i];
		if (previous == '<' && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
			nodes++;
		} else if (previous == '>' && c != '<') {
			nodes++;
		}
		if (c == '=') {
			attributes++;
		}
		previous = c;
	}
	bytes += size;
	last = previous;
}

vector<OpenFileInfo> GlobFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing) {
	vector<OpenFileInfo> files;
	for (const auto &pattern : patterns) {
		// Sorted for an order independent of the filesystem, as read_csv does. Readers keep it across threads.
		auto matched = fs.Glob(pattern, nullptr);
		if (matched.empty() && !skip_missing) {
			throw IOException("No files found that match the pattern \"%s\"", pattern);
		}
		std::sort(matched.begin(), matched.end(),
		          [](const OpenFileInfo &a, const OpenFileInfo &b) { return a.path < b.path; });
		for (auto &file : matched) {
			files.push_back(std::move(file));
		}
	}
	return files;
}

vector<string> ExpandFilePatterns(FileSystem &fs, const vector<string> &patterns, bool skip_missing) {
	vector<string> paths;
	for (auto &file : GlobFilePatterns(fs, patterns, skip_missing)) {
		paths.push_back(std::move(file.path));
	}
	return paths;
}

optional_idx GlobbedFileSize(const OpenFileInfo &file) {
	if (!file.extended_info) {
		return optional_idx();
	}
	auto entry = file.extended_info->options.find("file_size");
	if (entry == file.extended_info->options.end() || entry->second.IsNull()) {
		return optional_idx();
	}
	return optional_idx(entry->second.GetValue<idx_t>());
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

// Reads the file's first bytes into `prefix` and chooses its HTML encoding. A BOM or <meta> in the first bytes
// decides it. Otherwise the bytes are sniffed until a UTF-8 sequence decides it or the file ends. The parse replays
// `prefix` before it reads the handle on, so a pipe, which can't rewind, is read once. A seekable handle that is still
// undecided past SNIFF_KEEP_BYTES is sniffed to its end without keeping the bytes and rewound, leaving `prefix` empty.
static std::string DetectHTMLHandleEncoding(FileHandle &handle, std::string &prefix) {
	constexpr idx_t CHUNK_SIZE = idx_t(1) << 20;
	constexpr idx_t SNIFF_KEEP_BYTES = CHUNK_SIZE;
	prefix.resize(XMLUtils::HTML_ENCODING_SNIFF_BYTES);
	prefix.resize(ReadUpTo(handle, &prefix[0], prefix.size()));
	auto encoding = XMLUtils::DeclaredHTMLEncoding(prefix.data(), prefix.size());
	if (!encoding.empty()) {
		return encoding;
	}
	UndeclaredEncodingSniffer sniffer;
	sniffer.Feed(prefix.data(), prefix.size());
	bool rewinds = handle.CanSeek();
	while (!sniffer.Decided() && !(rewinds && prefix.size() >= SNIFF_KEEP_BYTES)) {
		auto offset = prefix.size();
		prefix.resize(offset + CHUNK_SIZE);
		auto got = ReadUpTo(handle, &prefix[offset], CHUNK_SIZE);
		prefix.resize(offset + got);
		if (got == 0) {
			return sniffer.Encoding();
		}
		sniffer.Feed(prefix.data() + offset, got);
	}
	if (sniffer.Decided()) {
		return sniffer.Encoding();
	}
	std::string().swap(prefix);
	std::string chunk(CHUNK_SIZE, '\0');
	while (!sniffer.Decided()) {
		auto got = ReadUpTo(handle, &chunk[0], CHUNK_SIZE);
		if (got == 0) {
			break;
		}
		sniffer.Feed(chunk.data(), got);
	}
	handle.Reset();
	return sniffer.Encoding();
}

struct FileHandleReader {
	explicit FileHandleReader(FileHandle &handle) : handle(handle) {
	}
	FileHandle &handle;
	// Bytes read from the handle before the parse began, which it reads first.
	std::string prefix;
	idx_t prefix_offset = 0;
	std::exception_ptr error;
	// Set when the parse reserves its DOM's estimated memory as it reads.
	DOMReserve reserve;
	DOMCostCounter cost;
	idx_t reserved = 0;

	int Delivered(const char *buffer, idx_t got) {
		if (reserve && got > 0) {
			cost.Feed(buffer, got);
			auto estimate = cost.Estimate();
			reserve(estimate - reserved);
			reserved = estimate;
		}
		return NumericCast<int>(got);
	}
};

// libxml2 calls this from C frames, so a read error is held and rethrown once the parse returns.
static int FileHandleReaderRead(void *context, char *buffer, int len) {
	auto &reader = *static_cast<FileHandleReader *>(context);
	try {
		if (reader.prefix_offset < reader.prefix.size()) {
			auto take = MinValue<idx_t>(UnsafeNumericCast<idx_t>(len), reader.prefix.size() - reader.prefix_offset);
			memcpy(buffer, reader.prefix.data() + reader.prefix_offset, take);
			reader.prefix_offset += take;
			if (reader.prefix_offset == reader.prefix.size()) {
				std::string().swap(reader.prefix);
				reader.prefix_offset = 0;
			}
			return reader.Delivered(buffer, take);
		}
		auto got = reader.handle.Read(buffer, UnsafeNumericCast<idx_t>(len));
		if (got < 0) {
			throw IOException("Could not read from file \"%s\"", reader.handle.path);
		}
		return reader.Delivered(buffer, UnsafeNumericCast<idx_t>(got));
	} catch (...) {
		reader.error = std::current_exception();
		return -1;
	}
}

XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html) {
	FileHandleReader reader(handle);
	std::string html_encoding = is_html ? DetectHTMLHandleEncoding(handle, reader.prefix) : "UTF-8";
	XMLDocRAII doc(FileHandleReaderRead, &reader, is_html, html_encoding);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

XMLDocPtr ParseHTMLFile(FileHandle &handle, const DOMReserve &reserve) {
	FileHandleReader reader(handle);
	reader.reserve = reserve;
	std::string encoding = DetectHTMLHandleEncoding(handle, reader.prefix);
	XMLDocPtr doc;
	try {
		doc = XMLUtils::ParseHTMLDocument(FileHandleReaderRead, &reader, encoding);
	} catch (...) {
		// A failed read ends the parse early, which libxml2 may report as an error of its own.
		if (reader.error) {
			std::rethrow_exception(reader.error);
		}
		throw;
	}
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

} // namespace duckdb
