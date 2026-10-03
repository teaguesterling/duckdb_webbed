//===----------------------------------------------------------------------===//
//                         sitting_duck
//
// named_parameter_compat.hpp
//
// Declaring and reading a table function's named parameters, on both DuckDB
// lines.
//
//===----------------------------------------------------------------------===//
//
// DuckDB v2.0 replaced the flat named-parameter map with a signature carrying
// typed kwargs:
//
//   v1.5   func.named_parameters["peek_size"] = LogicalType::INTEGER;
//   v2.0   func.GetSignature().WithTypedKwargs("options", [](TypedKwargs &o) {
//              o.Add(Identifier("peek_size"), LogicalType::INTEGER);
//          });
//
// and retyped the bind-time map from case_insensitive_map_t<Value> (keyed by
// string) to named_argument_map_t (keyed by Identifier), so `.at("peek_size")`
// no longer compiles: Identifier's constructor from string is explicit by
// design.
//
// WHY #if HERE, WHEN THE REST OF THE EXTENSION USES SFINAE PROBES.
// duckdb_adapter.cpp probes each accessor because the two lines express the SAME
// idea with different spellings, and a probe can absorb a spelling. This is not
// that: a flat map and a signature with named kwargs groups are different
// shapes, and the v2.0 spelling names types (TypedKwargs) that do not exist on
// v1.5 at all. A template cannot hide a type that is absent -- non-dependent
// names are looked up when the template is defined, not when it is instantiated.
// So the branch is a preprocessor one, and it is confined to this header rather
// than sprayed across four translation units.
//
// THE SENTINEL, and two ways of choosing one that do not work.
//
// duckdb/main/capi/capi_function_signature.hpp is absent on v1.5.6 and arrived
// on v2.0-cyanoptera in the same work that gave TableFunction its signature. It
// is still a PROXY for "does this DuckDB have typed kwargs" -- there is no header
// whose presence means exactly that, because TypedKwargs lives in
// duckdb/function/function.hpp, which both lines have.
//
// Rejected: duckdb/common/identifier.hpp. v1.5.6 BACKPORTED it, so it is present
// on both lines -- the partial-backport trap documented in duckdb_adapter.cpp.
//
// Rejected, and this one actually broke: duckdb/common/enums/identifier_case_mode.hpp.
// It is v2.0-only, so it looked correct. But it had already landed at cyanoptera
// e366461e30 while TableFunction::GetSignature had NOT, so the sentinel selected
// the v2.0 path against a DuckDB with no kwargs API and produced 99 errors. A
// sentinel must co-vary with the API it gates, not merely with the same major
// version. Verify a candidate at the commit you build against, not just on the
// branch tip.
//
// This code therefore targets v2.0-cyanoptera at or after the signature refactor.
// That branch moves hourly; if this stops compiling, check whether the kwargs API
// moved again before assuming the call sites are wrong.
//
#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/function/table_function.hpp"

#if __has_include("duckdb/main/capi/capi_function_signature.hpp")
#define WEBBED_HAS_TYPED_KWARGS 1
#endif

namespace duckdb {

//! One named parameter: the name callers write, and the type it accepts.
struct NamedParamSpec {
	const char *name;
	LogicalType type;
};

//! The name of the kwargs group v2.0 collects a table function's options under.
//! v1.5 has no grouping, so this is unused there.
static constexpr const char *WEBBED_KWARGS_GROUP = "options";

//! The type of TableFunctionBindInput::named_parameters on whichever line we are
//! building against. Spell the map this way in any signature that receives it:
//! naming named_parameter_map_t directly compiles on v1.5 and silently means a
//! DIFFERENT type on v2.0 (which still defines that name, as identifier_map_t,
//! while the bind input actually hands out named_argument_map_t).
#ifdef WEBBED_HAS_TYPED_KWARGS
using NamedParamMap = named_argument_map_t;
#else
using NamedParamMap = named_parameter_map_t;
#endif

#ifdef WEBBED_HAS_TYPED_KWARGS

//! Declare a table function's named parameters (v2.0: typed kwargs on the signature).
//! Call this ONCE per function, then Extend for any further parameters: v2.0
//! distinguishes creating the "**kwargs" parameter from adding options to it,
//! and ExtendTypedKwargs throws if the signature has no kwargs parameter yet.
inline void DeclareNamedParameters(TableFunction &func, const vector<NamedParamSpec> &params) {
	func.GetSignature().WithTypedKwargs(Identifier(WEBBED_KWARGS_GROUP), [&params](TypedKwargs &kwargs) {
		for (const auto &param : params) {
			kwargs.Add(Identifier(string(param.name)), param.type);
		}
	});
}

//! Add more named parameters to a function that has already declared some.
inline void ExtendNamedParameters(TableFunction &func, const vector<NamedParamSpec> &params) {
	func.GetSignature().ExtendTypedKwargs([&params](TypedKwargs &kwargs) {
		for (const auto &param : params) {
			kwargs.Add(Identifier(string(param.name)), param.type);
		}
	});
}

//! The value bound to `name`, or nullptr when the call left it out.
//! v2.0 keys this map by Identifier, and Identifier's string constructor is
//! explicit, so the key has to be built rather than passed as a literal.
inline const Value *FindNamedParameter(const named_argument_map_t &params, const char *name) {
	auto entry = params.find(Identifier(string(name)));
	if (entry == params.end()) {
		return nullptr;
	}
	return &entry->second;
}

#else

//! Declare a table function's named parameters (v1.5: the flat map).
inline void DeclareNamedParameters(TableFunction &func, const vector<NamedParamSpec> &params) {
	for (const auto &param : params) {
		func.named_parameters[param.name] = param.type;
	}
}

//! Add more named parameters to a function that has already declared some. On
//! this line there is no distinction -- the map does not care when a key arrives
//! -- but the call sites keep the same shape on both lines.
inline void ExtendNamedParameters(TableFunction &func, const vector<NamedParamSpec> &params) {
	DeclareNamedParameters(func, params);
}

//! The value bound to `name`, or nullptr when the call left it out.
inline const Value *FindNamedParameter(const named_parameter_map_t &params, const char *name) {
	auto entry = params.find(name);
	if (entry == params.end()) {
		return nullptr;
	}
	return &entry->second;
}

#endif

//! The value bound to `name`. Throws like map::at did, so the call sites that
//! already knew the parameter was present keep their shape.
template <class MAP>
inline const Value &NamedParamAt(const MAP &params, const char *name) {
	auto value = FindNamedParameter(params, name);
	if (!value) {
		throw InvalidInputException("named parameter '%s' was not provided", name);
	}
	return *value;
}

//! Whether the call provided `name` at all.
template <class MAP>
inline bool HasNamedParam(const MAP &params, const char *name) {
	return FindNamedParameter(params, name) != nullptr;
}

} // namespace duckdb
