#pragma once

#include "duckdb/common/typedefs.hpp"

namespace duckdb {

//! Chooses the encoding of HTML that declares none, from bytes fed in any number of chunks. The bytes are
//! windows-1252 when they hold a byte of 0x80 or more and no complete well-formed UTF-8 multibyte sequence, and UTF-8
//! otherwise, so a stray bad byte in UTF-8 text reads as U+FFFD rather than turning the whole file into mojibake.
class UndeclaredEncodingSniffer {
public:
	void Feed(const char *data, idx_t size) {
		for (idx_t i = 0; i < size && !found_utf8; i++) {
			FeedByte(static_cast<unsigned char>(data[i]));
		}
	}
	//! Whether the bytes fed so far already settle the encoding as UTF-8, so no later byte can change it.
	bool Decided() const {
		return found_utf8;
	}
	//! The encoding for every byte fed.
	const char *Encoding() const {
		return found_utf8 || !saw_high_byte ? "UTF-8" : "windows-1252";
	}

private:
	void FeedByte(unsigned char byte) {
		if (needed > 0) {
			if (byte >= lower && byte <= upper) {
				lower = 0x80;
				upper = 0xBF;
				if (--needed == 0) {
					found_utf8 = true;
				}
				return;
			}
			// The sequence broke off. The byte that broke it may start a new one.
			needed = 0;
		}
		if (byte < 0x80) {
			return;
		}
		saw_high_byte = true;
		lower = 0x80;
		upper = 0xBF;
		// Unicode Table 3-7, well-formed UTF-8 byte sequences. C0, C1 and F5-FF never lead one.
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
		}
	}

	idx_t needed = 0;
	unsigned char lower = 0x80;
	unsigned char upper = 0xBF;
	bool saw_high_byte = false;
	bool found_utf8 = false;
};

} // namespace duckdb
