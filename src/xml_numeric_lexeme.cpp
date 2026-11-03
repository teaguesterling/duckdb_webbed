#include "xml_numeric_lexeme.hpp"

#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/types/value.hpp"

#include <cmath>

namespace duckdb {

namespace {

// A decimal in positional or exponent notation, reduced to 0.digits * 10^exponent with no
// leading or trailing zeros in digits. Zero has empty digits.
struct SignificantDigits {
	bool negative = false;
	std::string digits;
	int64_t exponent = 0;

	bool operator==(const SignificantDigits &other) const {
		return negative == other.negative && digits == other.digits && exponent == other.exponent;
	}
};

SignificantDigits ReduceDecimal(const std::string &text) {
	SignificantDigits result;
	size_t i = 0;
	if (i < text.size() && text[i] == '-') {
		result.negative = true;
		i++;
	}
	int64_t point = 0;
	bool seen_point = false;
	for (; i < text.size() && text[i] != 'e' && text[i] != 'E'; i++) {
		if (text[i] == '.') {
			seen_point = true;
			continue;
		}
		if (!seen_point) {
			point++;
		}
		result.digits.push_back(text[i]);
	}
	int64_t exponent = 0;
	if (i < text.size()) {
		exponent = std::stoll(text.substr(i + 1));
	}
	size_t first = result.digits.find_first_not_of('0');
	if (first == std::string::npos) {
		result.digits.clear();
		return result;
	}
	result.digits = result.digits.substr(first, result.digits.find_last_not_of('0') - first + 1);
	result.exponent = point - static_cast<int64_t>(first) + exponent;
	return result;
}

// `0`, or an optional `-` then a nonzero digit then digits, ending at `end`.
bool IsCanonicalIntegerPart(const std::string &text, size_t end) {
	size_t i = (!text.empty() && text[0] == '-') ? 1 : 0;
	if (i >= end) {
		return false;
	}
	if (text[i] == '0') {
		return end == i + 1 && i == 0;
	}
	for (; i < end; i++) {
		if (text[i] < '0' || text[i] > '9') {
			return false;
		}
	}
	return true;
}

template <class T>
bool IntegerReproduces(const std::string &text) {
	T result;
	return IsCanonicalIntegerPart(text, text.size()) && TryCast::Operation(string_t(text), result, true);
}

bool DoubleReproduces(const std::string &text) {
	size_t point = text.find('.');
	if (point == std::string::npos) {
		point = text.size();
	} else if (point + 1 == text.size() || (text.back() == '0' && point + 2 != text.size())) {
		// `5.`, or trailing fractional zeros other than DuckDB's own `.0`
		return false;
	}
	// `-0.5` and `-0.0` are allowed. A bare `-0` is not, because neither INTEGER nor DOUBLE renders it.
	bool negative_zero_with_fraction = point == 2 && point < text.size() && text[0] == '-' && text[1] == '0';
	if (!negative_zero_with_fraction && !IsCanonicalIntegerPart(text, point)) {
		return false;
	}
	for (size_t i = point + 1; i < text.size(); i++) {
		if (text[i] < '0' || text[i] > '9') {
			return false;
		}
	}
	double value;
	if (!TryCast::Operation(string_t(text), value, true) || !std::isfinite(value)) {
		return false;
	}
	auto lexeme = ReduceDecimal(text);
	// At most 15 significant digits in the normal range always survive text -> double -> shortest
	// text unchanged (DBL_DIG), so only longer lexemes need the rendering.
	if (lexeme.digits.size() <= 15 && lexeme.exponent > -300 && lexeme.exponent < 300) {
		return true;
	}
	return lexeme == ReduceDecimal(Value::DOUBLE(value).ToString());
}

} // namespace

bool NumericTypeReproduces(const std::string &text, const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::INTEGER:
		return IntegerReproduces<int32_t>(text);
	case LogicalTypeId::BIGINT:
		return IntegerReproduces<int64_t>(text);
	case LogicalTypeId::DOUBLE:
		return DoubleReproduces(text);
	default:
		return true;
	}
}

} // namespace duckdb
