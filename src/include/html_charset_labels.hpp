#pragma once

#include <string>

namespace duckdb {

// The libxml2 encoding to decode HTML bytes with when a document declares `label` as its charset, or "" when the
// label should be ignored. Only labels in the WHATWG Encoding Standard count, as in a browser, so a name iconv happens
// to open (`unicode`, `utf-7`) is ignored, and so is `replacement` or a label libxml2 has no decoder for.
std::string HTMLDecoderForLabel(const std::string &label);

} // namespace duckdb
