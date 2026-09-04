#pragma once

#include "duckdb.hpp"
#include <type_traits>
#include <utility>

// Detect new DuckDB API (post v1.4) by checking for moved headers.
// When ListVector/StructVector moved to duckdb/common/vector/, the bind
// function signature and field accessors changed in the same DuckDB version.
#if __has_include("duckdb/common/vector/list_vector.hpp")
#define DUCKDB_HAS_NEW_VECTOR_HEADERS 1
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#endif

#if __has_include("duckdb/function/scalar_function.hpp")
#include "duckdb/function/scalar_function.hpp"
#endif

#if __has_include("duckdb/function/function_set.hpp")
#include "duckdb/function/function_set.hpp"
#endif

// Detect the duckdb::Identifier type (newer DuckDB main), which replaced std::string as the key
// of child_list_t (STRUCT/UNION field names) and several name-typed fields (e.g. table alias,
// Expression::GetAlias). Identifier does not implicitly convert to/from std::string, so reads and
// constructions at the boundary must go through the helpers below. Detected by header presence so
// it stays orthogonal to DUCKDB_HAS_NEW_VECTOR_HEADERS.
#if __has_include("duckdb/common/identifier.hpp")
#define DUCKDB_HAS_IDENTIFIER 1
#include "duckdb/common/identifier.hpp"
#endif

namespace duckdb {

// --- Identifier <-> string boundary helpers ---
// CompatIdentifierName: read the raw string name from a child_list_t key / aliased name.
// CompatMakeIdentifier: build a child_list_t key (or name-typed field) from a runtime string.
// On older DuckDB these are pass-throughs (the key already is a std::string).
#ifdef DUCKDB_HAS_IDENTIFIER
inline const string &CompatIdentifierName(const Identifier &id) {
	return id.GetIdentifierName();
}
inline const string &CompatIdentifierName(const string &name) {
	return name;
}
inline Identifier CompatMakeIdentifier(string name) {
	return Identifier(std::move(name));
}
#else
inline const string &CompatIdentifierName(const string &name) {
	return name;
}
inline string CompatMakeIdentifier(string name) {
	return name;
}
#endif

// --- bind-signature name type ---
// The SAME Identifier change also moved the column-name vector handed to table-function and
// COPY bind callbacks from vector<string> to vector<Identifier>. Spell that parameter
// vector<CompatName> in every bind DECLARATION and DEFINITION.
//
// Only the signatures move. Identifier's constructor from `const char *` is IMPLICIT (a literal
// is an identifier by intent) while its constructor from `string` is EXPLICIT (promoting a
// runtime string is a deliberate act), so `names.push_back("filename")` compiles unchanged and
// only the places a *runtime* string crosses the boundary need CompatMakeIdentifier /
// CompatIdentifierName. Deliberately NOT an implicit conversion -- the explicitness is the point
// of the upstream change.
#ifdef DUCKDB_HAS_IDENTIFIER
using CompatName = Identifier;
#else
using CompatName = string;
#endif

// Bulk form of CompatIdentifierName, for the places a bind function copies its whole `names`
// vector into a plain vector<string> held in bind data. `assign`/`=` no longer compiles once
// CompatName is Identifier, so the copy is element-wise.
inline vector<string> CompatNamesToStrings(const vector<CompatName> &names) {
	vector<string> result;
	result.reserve(names.size());
	for (auto &name : names) {
		result.push_back(CompatIdentifierName(name));
	}
	return result;
}

// --- LogicalType alias ---
// v1.5: void SetAlias(string)                -- mutates in place
// v2.0: LogicalType WithAlias(string) const  -- returns a copy, so a type whose type-info is
//       shared is never mutated behind another holder's back. SetAlias is REMOVED, not
//       deprecated.
//
// Probed for the MEMBER rather than keyed off DUCKDB_HAS_IDENTIFIER: these are independent
// upstream changes, and tying one to the other would silently pick the wrong branch if they ever
// land in different releases.
//
// TAG DISPATCH, NOT `if constexpr`. This extension compiles at C++11 on Linux ON PURPOSE (see the
// comment on the MSVC-only /std:c++17 block in CMakeLists.txt: forcing C++17 here makes
// static-const data members in duckdb's headers acquire implicit inline linkage in our TUs but
// not in libduckdb's C++11-built ones, which is a multiple-definition link error). Two overloaded
// function TEMPLATES give the same "only the taken branch is instantiated" property without it.
template <class T, class = void>
struct CompatHasWithAlias : std::false_type {};
template <class T>
struct CompatHasWithAlias<T, decltype(void(std::declval<const T &>().WithAlias(string())))> : std::true_type {};

template <class TYPE>
inline LogicalType CompatWithAliasImpl(TYPE type, string alias, std::true_type) {
	return type.WithAlias(std::move(alias)); // v2.0: returns a copy
}
template <class TYPE>
inline LogicalType CompatWithAliasImpl(TYPE type, string alias, std::false_type) {
	type.SetAlias(std::move(alias)); // v1.5: mutates in place
	return type;
}

template <class TYPE>
inline LogicalType CompatWithAlias(TYPE type, string alias) {
	return CompatWithAliasImpl(std::move(type), std::move(alias), CompatHasWithAlias<TYPE>());
}

#ifdef DUCKDB_HAS_NEW_VECTOR_HEADERS

// --- Bind function signature ---
#define DUCKDB_SCALAR_BIND_PARAMS  BindScalarFunctionInput &bind_input
#define DUCKDB_SCALAR_BIND_CONTEXT bind_input.GetClientContext()
#define DUCKDB_SCALAR_BIND_ARGS    bind_input.GetArguments()

// --- ScalarFunction property setters (fields are now private) ---
inline void SetScalarFunctionNullHandling(ScalarFunction &func, FunctionNullHandling handling) {
	func.SetNullHandling(handling);
}
inline void SetScalarFunctionVarArgs(ScalarFunction &func, LogicalType varargs) {
	func.SetVarArgs(std::move(varargs));
}

// --- Vector helpers ---
// ListVector::GetEntry deprecated in favor of GetChild
inline Vector &CompatListGetChild(Vector &v) {
	return ListVector::GetChildMutable(v);
}
// ToUnifiedFormat lost the count parameter
inline void CompatToUnifiedFormat(Vector &v, idx_t /*count*/, UnifiedVectorFormat &data) {
	v.ToUnifiedFormat(data);
}
// StructVector::GetEntries returns vector<Vector>& (not vector<unique_ptr<Vector>>&)
inline Vector &CompatStructGetField(Vector &v, idx_t field_idx) {
	return StructVector::GetEntries(v)[field_idx];
}

// --- Output chunk finalization ---
// DuckDB main requires vector buffers to have their size set after SetValue writes.
// SetChildCardinality sets both chunk count and FlatVector::SetSize on each column.
inline void CompatSetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetChildCardinality(count);
}

// --- Constant folding workaround ---
// DuckDB main's VectorStructBuffer::SetVectorType throws InternalException when
// the optimizer constant-folds functions returning STRUCT-containing types
// (LIST(STRUCT), STRUCT, MAP). Mark them VOLATILE to skip constant folding.
//
// There is deliberately NO FunctionSet overload. DuckDB v2.0's FunctionSet<T>::functions yields
// shared_ptr<const T>, so a set cannot hand out a mutable member to configure after the fact:
//
//   error: 'class duckdb::shared_ptr<const duckdb::ScalarFunction>' has no member
//          named 'SetStability'
//
// Shimming that would be wrong rather than merely awkward -- the supported shape is to finish
// configuring each ScalarFunction BEFORE it goes into the set. Call this on the function, then
// AddFunction it.
inline void PreventStructConstantFolding(ScalarFunction &func) {
	func.SetStability(FunctionStability::VOLATILE);
}

// --- Type promotion ---
// ForceMaxLogicalType(left, right) now requires a ClientContext (to consult extension-registered
// casts). DefaultForceMaxLogicalType is the context-free variant using only built-in CastRules,
// which matches the semantics of the old 2-argument overload.
inline LogicalType CompatForceMaxLogicalType(const LogicalType &left, const LogicalType &right) {
	return LogicalType::DefaultForceMaxLogicalType(left, right);
}

#else // Old API

#define DUCKDB_SCALAR_BIND_PARAMS                                                                                      \
	ClientContext &context, ScalarFunction &bound_function, vector<unique_ptr<Expression>> &arguments
#define DUCKDB_SCALAR_BIND_CONTEXT context
#define DUCKDB_SCALAR_BIND_ARGS    arguments

inline void SetScalarFunctionNullHandling(ScalarFunction &func, FunctionNullHandling handling) {
	func.null_handling = handling;
}
inline void SetScalarFunctionVarArgs(ScalarFunction &func, LogicalType varargs) {
	func.varargs = std::move(varargs);
}

inline Vector &CompatListGetChild(Vector &v) {
	return ListVector::GetEntry(v);
}
inline void CompatToUnifiedFormat(Vector &v, idx_t count, UnifiedVectorFormat &data) {
	v.ToUnifiedFormat(count, data);
}
inline Vector &CompatStructGetField(Vector &v, idx_t field_idx) {
	return *StructVector::GetEntries(v)[field_idx];
}

inline void CompatSetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetCardinality(count);
}

// No-op on old API — constant folding works fine for complex types
inline void PreventStructConstantFolding(ScalarFunction &) {
}

inline LogicalType CompatForceMaxLogicalType(const LogicalType &left, const LogicalType &right) {
	return LogicalType::ForceMaxLogicalType(left, right);
}

#endif

// Add a function to a function set with the VOLATILE marking applied to the function FIRST.
// This is the only order that works on DuckDB v2.0, where FunctionSet<T>::functions yields
// shared_ptr<const T> and a set member can no longer be configured after it has been added.
// Defined outside the #ifdef because it delegates to whichever PreventStructConstantFolding
// overload the branch above selected (a no-op on the old API).
template <typename T>
inline void PreventStructConstantFoldingAndAdd(FunctionSet<T> &func_set, T func) {
	PreventStructConstantFolding(func);
	func_set.AddFunction(std::move(func));
}

} // namespace duckdb
