#include "html_charset_labels.hpp"

#include <libxml/encoding.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace duckdb {

namespace {

struct LabelEntry {
	const char *label;
	const char *name;
};

// The labels of the WHATWG Encoding Standard (https://encoding.spec.whatwg.org/#names-and-labels), sorted by label in
// byte order, each with the name of the encoding it selects.
const LabelEntry LABELS[] = {
    {"866", "IBM866"},
    {"ansi_x3.4-1968", "windows-1252"},
    {"arabic", "ISO-8859-6"},
    {"ascii", "windows-1252"},
    {"asmo-708", "ISO-8859-6"},
    {"big5", "Big5"},
    {"big5-hkscs", "Big5"},
    {"chinese", "GBK"},
    {"cn-big5", "Big5"},
    {"cp1250", "windows-1250"},
    {"cp1251", "windows-1251"},
    {"cp1252", "windows-1252"},
    {"cp1253", "windows-1253"},
    {"cp1254", "windows-1254"},
    {"cp1255", "windows-1255"},
    {"cp1256", "windows-1256"},
    {"cp1257", "windows-1257"},
    {"cp1258", "windows-1258"},
    {"cp819", "windows-1252"},
    {"cp866", "IBM866"},
    {"csbig5", "Big5"},
    {"cseuckr", "EUC-KR"},
    {"cseucpkdfmtjapanese", "EUC-JP"},
    {"csgb2312", "GBK"},
    {"csibm866", "IBM866"},
    {"csiso2022jp", "ISO-2022-JP"},
    {"csiso2022kr", "replacement"},
    {"csiso58gb231280", "GBK"},
    {"csiso88596e", "ISO-8859-6"},
    {"csiso88596i", "ISO-8859-6"},
    {"csiso88598e", "ISO-8859-8"},
    {"csiso88598i", "ISO-8859-8-I"},
    {"csisolatin1", "windows-1252"},
    {"csisolatin2", "ISO-8859-2"},
    {"csisolatin3", "ISO-8859-3"},
    {"csisolatin4", "ISO-8859-4"},
    {"csisolatin5", "windows-1254"},
    {"csisolatin6", "ISO-8859-10"},
    {"csisolatin9", "ISO-8859-15"},
    {"csisolatinarabic", "ISO-8859-6"},
    {"csisolatincyrillic", "ISO-8859-5"},
    {"csisolatingreek", "ISO-8859-7"},
    {"csisolatinhebrew", "ISO-8859-8"},
    {"cskoi8r", "KOI8-R"},
    {"csksc56011987", "EUC-KR"},
    {"csmacintosh", "macintosh"},
    {"csshiftjis", "Shift_JIS"},
    {"csunicode", "UTF-16LE"},
    {"cyrillic", "ISO-8859-5"},
    {"dos-874", "windows-874"},
    {"ecma-114", "ISO-8859-6"},
    {"ecma-118", "ISO-8859-7"},
    {"elot_928", "ISO-8859-7"},
    {"euc-jp", "EUC-JP"},
    {"euc-kr", "EUC-KR"},
    {"gb18030", "gb18030"},
    {"gb2312", "GBK"},
    {"gb_2312", "GBK"},
    {"gb_2312-80", "GBK"},
    {"gbk", "GBK"},
    {"greek", "ISO-8859-7"},
    {"greek8", "ISO-8859-7"},
    {"hebrew", "ISO-8859-8"},
    {"hz-gb-2312", "replacement"},
    {"ibm819", "windows-1252"},
    {"ibm866", "IBM866"},
    {"iso-10646-ucs-2", "UTF-16LE"},
    {"iso-2022-cn", "replacement"},
    {"iso-2022-cn-ext", "replacement"},
    {"iso-2022-jp", "ISO-2022-JP"},
    {"iso-2022-kr", "replacement"},
    {"iso-8859-1", "windows-1252"},
    {"iso-8859-10", "ISO-8859-10"},
    {"iso-8859-11", "windows-874"},
    {"iso-8859-13", "ISO-8859-13"},
    {"iso-8859-14", "ISO-8859-14"},
    {"iso-8859-15", "ISO-8859-15"},
    {"iso-8859-16", "ISO-8859-16"},
    {"iso-8859-2", "ISO-8859-2"},
    {"iso-8859-3", "ISO-8859-3"},
    {"iso-8859-4", "ISO-8859-4"},
    {"iso-8859-5", "ISO-8859-5"},
    {"iso-8859-6", "ISO-8859-6"},
    {"iso-8859-6-e", "ISO-8859-6"},
    {"iso-8859-6-i", "ISO-8859-6"},
    {"iso-8859-7", "ISO-8859-7"},
    {"iso-8859-8", "ISO-8859-8"},
    {"iso-8859-8-e", "ISO-8859-8"},
    {"iso-8859-8-i", "ISO-8859-8-I"},
    {"iso-8859-9", "windows-1254"},
    {"iso-ir-100", "windows-1252"},
    {"iso-ir-101", "ISO-8859-2"},
    {"iso-ir-109", "ISO-8859-3"},
    {"iso-ir-110", "ISO-8859-4"},
    {"iso-ir-126", "ISO-8859-7"},
    {"iso-ir-127", "ISO-8859-6"},
    {"iso-ir-138", "ISO-8859-8"},
    {"iso-ir-144", "ISO-8859-5"},
    {"iso-ir-148", "windows-1254"},
    {"iso-ir-149", "EUC-KR"},
    {"iso-ir-157", "ISO-8859-10"},
    {"iso-ir-58", "GBK"},
    {"iso8859-1", "windows-1252"},
    {"iso8859-10", "ISO-8859-10"},
    {"iso8859-11", "windows-874"},
    {"iso8859-13", "ISO-8859-13"},
    {"iso8859-14", "ISO-8859-14"},
    {"iso8859-15", "ISO-8859-15"},
    {"iso8859-2", "ISO-8859-2"},
    {"iso8859-3", "ISO-8859-3"},
    {"iso8859-4", "ISO-8859-4"},
    {"iso8859-5", "ISO-8859-5"},
    {"iso8859-6", "ISO-8859-6"},
    {"iso8859-7", "ISO-8859-7"},
    {"iso8859-8", "ISO-8859-8"},
    {"iso8859-9", "windows-1254"},
    {"iso88591", "windows-1252"},
    {"iso885910", "ISO-8859-10"},
    {"iso885911", "windows-874"},
    {"iso885913", "ISO-8859-13"},
    {"iso885914", "ISO-8859-14"},
    {"iso885915", "ISO-8859-15"},
    {"iso88592", "ISO-8859-2"},
    {"iso88593", "ISO-8859-3"},
    {"iso88594", "ISO-8859-4"},
    {"iso88595", "ISO-8859-5"},
    {"iso88596", "ISO-8859-6"},
    {"iso88597", "ISO-8859-7"},
    {"iso88598", "ISO-8859-8"},
    {"iso88599", "windows-1254"},
    {"iso_8859-1", "windows-1252"},
    {"iso_8859-15", "ISO-8859-15"},
    {"iso_8859-1:1987", "windows-1252"},
    {"iso_8859-2", "ISO-8859-2"},
    {"iso_8859-2:1987", "ISO-8859-2"},
    {"iso_8859-3", "ISO-8859-3"},
    {"iso_8859-3:1988", "ISO-8859-3"},
    {"iso_8859-4", "ISO-8859-4"},
    {"iso_8859-4:1988", "ISO-8859-4"},
    {"iso_8859-5", "ISO-8859-5"},
    {"iso_8859-5:1988", "ISO-8859-5"},
    {"iso_8859-6", "ISO-8859-6"},
    {"iso_8859-6:1987", "ISO-8859-6"},
    {"iso_8859-7", "ISO-8859-7"},
    {"iso_8859-7:1987", "ISO-8859-7"},
    {"iso_8859-8", "ISO-8859-8"},
    {"iso_8859-8:1988", "ISO-8859-8"},
    {"iso_8859-9", "windows-1254"},
    {"iso_8859-9:1989", "windows-1254"},
    {"koi", "KOI8-R"},
    {"koi8", "KOI8-R"},
    {"koi8-r", "KOI8-R"},
    {"koi8-ru", "KOI8-U"},
    {"koi8-u", "KOI8-U"},
    {"koi8_r", "KOI8-R"},
    {"korean", "EUC-KR"},
    {"ks_c_5601-1987", "EUC-KR"},
    {"ks_c_5601-1989", "EUC-KR"},
    {"ksc5601", "EUC-KR"},
    {"ksc_5601", "EUC-KR"},
    {"l1", "windows-1252"},
    {"l2", "ISO-8859-2"},
    {"l3", "ISO-8859-3"},
    {"l4", "ISO-8859-4"},
    {"l5", "windows-1254"},
    {"l6", "ISO-8859-10"},
    {"l9", "ISO-8859-15"},
    {"latin1", "windows-1252"},
    {"latin2", "ISO-8859-2"},
    {"latin3", "ISO-8859-3"},
    {"latin4", "ISO-8859-4"},
    {"latin5", "windows-1254"},
    {"latin6", "ISO-8859-10"},
    {"logical", "ISO-8859-8-I"},
    {"mac", "macintosh"},
    {"macintosh", "macintosh"},
    {"ms932", "Shift_JIS"},
    {"ms_kanji", "Shift_JIS"},
    {"replacement", "replacement"},
    {"shift-jis", "Shift_JIS"},
    {"shift_jis", "Shift_JIS"},
    {"sjis", "Shift_JIS"},
    {"sun_eu_greek", "ISO-8859-7"},
    {"tis-620", "windows-874"},
    {"ucs-2", "UTF-16LE"},
    {"unicode", "UTF-16LE"},
    {"unicode-1-1-utf-8", "UTF-8"},
    {"unicode11utf8", "UTF-8"},
    {"unicode20utf8", "UTF-8"},
    {"unicodefeff", "UTF-16LE"},
    {"unicodefffe", "UTF-16BE"},
    {"us-ascii", "windows-1252"},
    {"utf-16", "UTF-16LE"},
    {"utf-16be", "UTF-16BE"},
    {"utf-16le", "UTF-16LE"},
    {"utf-8", "UTF-8"},
    {"utf8", "UTF-8"},
    {"visual", "ISO-8859-8"},
    {"windows-1250", "windows-1250"},
    {"windows-1251", "windows-1251"},
    {"windows-1252", "windows-1252"},
    {"windows-1253", "windows-1253"},
    {"windows-1254", "windows-1254"},
    {"windows-1255", "windows-1255"},
    {"windows-1256", "windows-1256"},
    {"windows-1257", "windows-1257"},
    {"windows-1258", "windows-1258"},
    {"windows-31j", "Shift_JIS"},
    {"windows-874", "windows-874"},
    {"windows-949", "EUC-KR"},
    {"x-cp1250", "windows-1250"},
    {"x-cp1251", "windows-1251"},
    {"x-cp1252", "windows-1252"},
    {"x-cp1253", "windows-1253"},
    {"x-cp1254", "windows-1254"},
    {"x-cp1255", "windows-1255"},
    {"x-cp1256", "windows-1256"},
    {"x-cp1257", "windows-1257"},
    {"x-cp1258", "windows-1258"},
    {"x-euc-jp", "EUC-JP"},
    {"x-gbk", "GBK"},
    {"x-mac-cyrillic", "x-mac-cyrillic"},
    {"x-mac-roman", "macintosh"},
    {"x-mac-ukrainian", "x-mac-cyrillic"},
    {"x-sjis", "Shift_JIS"},
    {"x-unicode20utf8", "UTF-8"},
    {"x-user-defined", "x-user-defined"},
    {"x-x-big5", "Big5"},
};

bool IsHTMLSpace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

bool OpensInLibxml2(const std::string &name) {
	xmlCharEncodingHandlerPtr handler = nullptr;
	if (xmlOpenCharEncodingHandler(name.c_str(), 0, &handler) != XML_ERR_OK) {
		return false;
	}
	if (handler) {
		xmlCharEncCloseFunc(handler);
	}
	return true;
}

// The names libxml2 may know a WHATWG encoding by, best first. libxml2 takes whatever iconv does, and iconv names
// the same table differently across platforms.
std::vector<std::string> DecoderCandidates(const std::string &name) {
	if (name == "replacement") {
		return {};
	}
	// A <meta> is read as ASCII, so the bytes cannot be UTF-16, and HTML treats that declaration as UTF-8.
	if (name == "UTF-16BE" || name == "UTF-16LE") {
		return {"UTF-8"};
	}
	// The HTML prescan reads x-user-defined as windows-1252.
	if (name == "x-user-defined") {
		return {"windows-1252", "CP1252"};
	}
	if (name == "IBM866") {
		return {"IBM866", "CP866"};
	}
	if (name == "ISO-8859-8-I") {
		return {"ISO-8859-8"};
	}
	if (name == "macintosh") {
		return {"MACINTOSH", "MACROMAN"};
	}
	if (name == "x-mac-cyrillic") {
		return {"MACUKRAINIAN", "MAC-UK", "MACCYRILLIC"};
	}
	if (name == "GBK" || name == "gb18030") {
		return {"GB18030"};
	}
	if (name == "Big5") {
		return {"BIG5-HKSCS", "BIG5HKSCS", "BIG5"};
	}
	if (name == "Shift_JIS") {
		return {"WINDOWS-31J", "CP932", "SHIFT_JIS"};
	}
	if (name == "EUC-KR") {
		return {"CP949", "UHC", "EUC-KR"};
	}
	if (name.compare(0, 8, "windows-") == 0) {
		return {name, "CP" + name.substr(8)};
	}
	return {name};
}

std::string ResolveDecoder(const std::string &name) {
	for (auto &candidate : DecoderCandidates(name)) {
		if (OpensInLibxml2(candidate)) {
			return candidate;
		}
	}
	return "";
}

// Built on first use, which C++ makes thread-safe, and read-only after.
const std::unordered_map<std::string, std::string> &DecodersByName() {
	static const std::unordered_map<std::string, std::string> decoders = [] {
		std::unordered_map<std::string, std::string> result;
		for (auto &entry : LABELS) {
			if (!result.count(entry.name)) {
				result[entry.name] = ResolveDecoder(entry.name);
			}
		}
		return result;
	}();
	return decoders;
}

} // namespace

std::string HTMLDecoderForLabel(const std::string &label) {
	size_t begin = 0;
	size_t end = label.size();
	while (begin < end && IsHTMLSpace(label[begin])) {
		begin++;
	}
	while (end > begin && IsHTMLSpace(label[end - 1])) {
		end--;
	}
	std::string key = label.substr(begin, end - begin);
	for (auto &c : key) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	auto entry =
	    std::lower_bound(std::begin(LABELS), std::end(LABELS), key,
	                     [](const LabelEntry &lhs, const std::string &rhs) { return rhs.compare(lhs.label) > 0; });
	if (entry == std::end(LABELS) || key != entry->label) {
		return "";
	}
	return DecodersByName().at(entry->name);
}

} // namespace duckdb
