#pragma once

#include <cstddef>
#include <cstring>

namespace duckdb {

// Feed libxml2's IO-based parsers (xmlCtxtReadIO / htmlReadIO) from an already-in-memory
// document. The IO entry points take no total-size parameter — unlike xmlCtxtReadMemory /
// htmlReadMemory, whose length argument is an `int`, so a buffer larger than INT_MAX
// (~2.147 GiB) wraps to a negative size and libxml2 rejects it as malformed. The callback's
// per-call length is always a small, safe int, so documents up to DuckDB's 4 GiB single-value
// cap parse correctly. (libxml2 DOM parsing itself remains memory-bound.)
struct XMLInMemoryReader {
	const char *data;
	size_t size;
	size_t pos;
};

// xmlInputReadCallback: copy up to `len` bytes into `buffer`, advancing `pos`; return the count
// (0 at EOF, -1 on a negative request). Never reads past the buffer.
inline int XMLInMemoryReaderRead(void *context, char *buffer, int len) {
	if (len < 0) {
		return -1;
	}
	auto *reader = static_cast<XMLInMemoryReader *>(context);
	size_t remaining = reader->size - reader->pos;
	size_t n = remaining < static_cast<size_t>(len) ? remaining : static_cast<size_t>(len);
	if (n > 0) {
		memcpy(buffer, reader->data + reader->pos, n);
		reader->pos += n;
	}
	return static_cast<int>(n);
}

// xmlInputCloseCallback: nothing to release (the buffer is owned by the caller).
inline int XMLInMemoryReaderClose(void *context) {
	return 0;
}

} // namespace duckdb
