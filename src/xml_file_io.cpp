#include "xml_file_io.hpp"

#include "duckdb/common/file_open_flags.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb_compat.hpp"
#include "utf8proc_wrapper.hpp"
#include "xml_in_memory_reader.hpp"
#include "xml_utils.hpp"

#include <algorithm>
#include <exception>

namespace duckdb {

idx_t DOMsThatFit(ClientContext &context, idx_t file_size) {
	// Divided in two steps so the product can't overflow
	auto memory = BufferManager::GetBufferManager(context).GetMaxMemory();
	return memory / DOM_BYTES_PER_FILE_BYTE / MaxValue<idx_t>(file_size, 1);
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

// Validates UTF-8 fed in chunks. A sequence a chunk cuts off is held back and checked once the next chunk completes it.
class Utf8StreamValidator {
public:
	bool Feed(const char *data, idx_t size) {
		idx_t offset = 0;
		if (!pending.empty()) {
			idx_t need = Utf8SequenceLength(static_cast<unsigned char>(pending[0])) - pending.size();
			idx_t take = MinValue(need, size);
			pending.append(data, take);
			offset = take;
			if (take < need) {
				return true;
			}
			if (Utf8Proc::Analyze(pending.data(), pending.size()) == UnicodeType::INVALID) {
				return false;
			}
			pending.clear();
		}
		idx_t cut = size;
		for (idx_t back = 1; back <= 3 && back <= size - offset; back++) {
			auto len = Utf8SequenceLength(static_cast<unsigned char>(data[size - back]));
			if (len == 0) {
				continue;
			}
			if (len > back) {
				cut = size - back;
			}
			break;
		}
		if (Utf8Proc::Analyze(data + offset, cut - offset) == UnicodeType::INVALID) {
			return false;
		}
		pending.assign(data + cut, size - cut);
		return true;
	}
	// Whether the input ended on a whole sequence.
	bool Finish() const {
		return pending.empty();
	}

private:
	std::string pending;
};

// Whether the rest of the handle, after `carry` bytes already in `buffer`, is valid UTF-8. Reads until the first
// invalid sequence.
static bool RestIsValidUtf8(FileHandle &handle, std::string &buffer, idx_t carry) {
	constexpr idx_t CHUNK_SIZE = idx_t(1) << 20;
	Utf8StreamValidator validator;
	if (!validator.Feed(buffer.data(), carry)) {
		return false;
	}
	buffer.resize(CHUNK_SIZE);
	while (true) {
		idx_t got = ReadUpTo(handle, &buffer[0], CHUNK_SIZE);
		if (got == 0) {
			return validator.Finish();
		}
		if (!validator.Feed(buffer.data(), got)) {
			return false;
		}
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

struct FileHandleReader {
	FileHandle &handle;
	std::exception_ptr error;
	// Set when the bytes are UTF-8, which libxml2 doesn't validate.
	unique_ptr<Utf8StreamValidator> utf8;
};

// libxml2 calls this from C frames, so a read error is held and rethrown once the parse returns.
static int FileHandleReaderRead(void *context, char *buffer, int len) {
	auto &reader = *static_cast<FileHandleReader *>(context);
	try {
		auto got = reader.handle.Read(buffer, UnsafeNumericCast<idx_t>(len));
		if (reader.utf8 && got >= 0) {
			bool valid = got == 0 ? reader.utf8->Finish() : reader.utf8->Feed(buffer, UnsafeNumericCast<idx_t>(got));
			if (!valid) {
				throw XMLUtils::InvalidHTMLBytes("UTF-8");
			}
		}
		return NumericCast<int>(got);
	} catch (...) {
		reader.error = std::current_exception();
		return -1;
	}
}

// A validator for HTML read as UTF-8. XML input is left to libxml2's own UTF-8 checks.
static unique_ptr<Utf8StreamValidator> HTMLUtf8Validator(bool is_html, const std::string &encoding) {
	return is_html && encoding == "UTF-8" ? make_uniq<Utf8StreamValidator>() : nullptr;
}

XMLDocRAII ParseXMLFile(FileHandle &handle, bool is_html) {
	std::string html_encoding = is_html ? DetectHTMLHandleEncoding(handle) : "UTF-8";
	FileHandleReader reader {handle, nullptr, HTMLUtf8Validator(is_html, html_encoding)};
	XMLDocRAII doc(FileHandleReaderRead, &reader, is_html, html_encoding);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

XMLDocPtr ParseHTMLFile(FileHandle &handle) {
	std::string encoding = DetectHTMLHandleEncoding(handle);
	FileHandleReader reader {handle, nullptr, HTMLUtf8Validator(true, encoding)};
	auto doc = XMLUtils::ParseHTMLDocument(FileHandleReaderRead, &reader, encoding);
	if (reader.error) {
		std::rethrow_exception(reader.error);
	}
	return doc;
}

} // namespace duckdb
