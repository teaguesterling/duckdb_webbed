#pragma once

#include "duckdb.hpp"

#include <string>

namespace duckdb {

// Whether reading `text` into `type` keeps every character of it recoverable. Inference types a
// column numeric only when this holds for every sample, and extraction into an inferred numeric
// column rejects values for which it does not.
//
// INTEGER and BIGINT take `0` or an optional `-`, a nonzero digit, then digits, inside the type's
// range: the exact text DuckDB renders for the value. `007`, `+5`, `-0`, `0x10`, and `1_000`
// cast to integers but cannot be read back. They are rejected.
//
// DOUBLE takes the same integer part plus an optional fraction, positional notation only, whose
// digits are exactly the shortest round-trip digits of the parsed double. That rejects trailing
// fractional zeros (`1.10`), digits a double cannot hold (`3.14159265358979323846`), exponents
// (`1e3`), `.5`, `5.`, `inf`, and `nan`. A fraction of a single `0` (`1.0`) is DuckDB's own
// rendering and is accepted. So is an integer lexeme the double represents exactly, which is why `1`
// next to `2.5` still types DOUBLE.
//
// Any other type returns true.
bool NumericTypeReproduces(const std::string &text, const LogicalType &type);

} // namespace duckdb
