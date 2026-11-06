#pragma once

#include "duckdb/common/typedefs.hpp"

namespace duckdb {

//! Chooses the encoding of HTML that declares none, from bytes fed in any number of chunks. The bytes are UTF-8 when
//! well-formed UTF-8 multibyte sequences are at least as many as the high bytes outside one, so a stray bad byte in
//! UTF-8 text reads as U+FFFD, and a UTF-8-shaped pair in windows-1252 text, such as "ß“", doesn't make it UTF-8.
class UndeclaredEncodingSniffer {
public:
	void Feed(const char *data, idx_t size) {
		for (idx_t i = 0; i < size; i++) {
			FeedByte(static_cast<unsigned char>(data[i]));
		}
	}
	//! The encoding for every byte fed.
	const char *Encoding() const {
		idx_t unfinished = needed > 0 ? 1 : 0;
		return sequences >= invalid + unfinished ? "UTF-8" : "windows-1252";
	}

private:
	void FeedByte(unsigned char byte) {
		if (needed > 0) {
			if (byte >= lower && byte <= upper) {
				lower = 0x80;
				upper = 0xBF;
				if (--needed == 0) {
					sequences++;
				}
				return;
			}
			// The sequence broke off. The byte that broke it may start a new one.
			invalid++;
			needed = 0;
		}
		if (byte < 0x80) {
			return;
		}
		lower = 0x80;
		upper = 0xBF;
		// Unicode Table 3-7, well-formed UTF-8 byte sequences. Continuation bytes, C0, C1 and F5-FF never lead one.
		if (byte >= 0xC2 && byte <= 0xDF) {
			needed = 1;
		} else if (byte >= 0xE0 && byte <= 0xEF) {
			needed = 2;
			lower = byte == 0xE0 ? 0xA0 : 0x80;
			upper = byte == 0xED ? 0x9F : 0xBF;
		} else if (byte >= 0xF0 && byte <= 0xF4) {
			needed = 3;
			lower = byte == 0xF0 ? 0x90 : 0x80;
			upper = byte == 0xF4 ? 0x8F : 0xBF;
		} else {
			invalid++;
		}
	}

	idx_t needed = 0;
	unsigned char lower = 0x80;
	unsigned char upper = 0xBF;
	idx_t sequences = 0;
	idx_t invalid = 0;
};

} // namespace duckdb
