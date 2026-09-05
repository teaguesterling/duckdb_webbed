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

// duckdb::Identifier is a name type that, on DuckDB main, replaced std::string as the key of
// child_list_t (STRUCT/UNION field names), as the element of table-function bind name vectors,
// and as several name-typed fields (table alias, Expression::GetAlias). It does not implicitly
// convert to/from std::string, so every read and construction at those boundaries goes through a
// helper below.
//
// DO NOT use this macro to decide WHICH name type a boundary uses. The header's existence is a
// PROXY for the change, not the change itself, and the two have already come apart upstream:
//
//   v1.5-variegata @ b155d6f63c (our pin)  no identifier.hpp   bind/keys/alias: string
//   v1.5-variegata @ tip f3be2750f5        HAS identifier.hpp  bind/keys/alias: STILL string
//   main (v2.0)                            HAS identifier.hpp  bind/keys/alias: Identifier
//
// identifier.hpp was backported to the stable branch WITHOUT the signature changes. Anything
// keyed on __has_include therefore flips to Identifier on a DuckDB that still wants string, and
// the whole extension stops compiling on the next submodule bump. Each boundary below instead
// asks DuckDB what type its OWN container holds, which cannot drift because it IS the thing
// that changes.
//
// This macro's only remaining job is to gate whether an Identifier overload can EXIST.
#if __has_include("duckdb/common/identifier.hpp")
#define DUCKDB_HAS_IDENTIFIER 1
#include "duckdb/common/identifier.hpp"
#endif

#if __has_include("duckdb/function/table_function.hpp")
#include "duckdb/function/table_function.hpp"
#endif
#if __has_include("duckdb/parser/tableref.hpp")
#include "duckdb/parser/tableref.hpp"
#endif

namespace duckdb {

// --- name types, each derived from the container that actually holds it ---
// These are three INDEPENDENT boundaries. They happen to move together on main and to be string
// on both v1.5 refs, but they are separate declarations upstream and are probed separately here,
// for the same reason the macro above must not decide any of them.

//! child_list_t key: STRUCT/UNION field names.
using CompatFieldName = typename child_list_t<int>::value_type::first_type;

//! Element of the `names` out-parameter of a table-function bind callback. Spell that parameter
//! vector<CompatName> in every bind DECLARATION and DEFINITION.
using CompatName = typename std::remove_reference<decltype(
    std::declval<TableFunctionBindInput &>().input_table_names)>::type::value_type;

//! Type of TableRef::alias, set by a bind_replace callback.
using CompatAliasName = decltype(TableRef::alias);

// --- Identifier <-> string boundary helpers ---
// CompatIdentifierName: read the raw string name back out of any of the above.
// CompatMakeIdentifier / CompatMakeName / CompatMakeAlias: build one from a runtime string.
//
// Only the READ side needs the macro, and only so an Identifier overload can exist at all. The
// string overload is always present: on a DuckDB that has Identifier but still types these
// boundaries as string, BOTH overloads are needed and they are unambiguous because the argument
// types are distinct.
#ifdef DUCKDB_HAS_IDENTIFIER
inline const string &CompatIdentifierName(const Identifier &id) {
	return id.GetIdentifierName();
}
#endif
inline const string &CompatIdentifierName(const string &name) {
	return name;
}

// The WRITE side needs no macro at all: each helper names its own boundary's type, so it yields
// a string or an Identifier according to what that boundary actually holds. Both spellings work
// as a constructor call -- Identifier(string) is the explicit ctor, string(string) is the copy.
inline CompatFieldName CompatMakeIdentifier(string name) {
	return CompatFieldName(std::move(name));
}
inline CompatName CompatMakeName(string name) {
	return CompatName(std::move(name));
}
inline CompatAliasName CompatMakeAlias(string name) {
	return CompatAliasName(std::move(name));
}

// Only the bind SIGNATURES move. Identifier's constructor from `const char *` is IMPLICIT (a
// literal is an identifier by intent) while its constructor from `string` is EXPLICIT (promoting
// a runtime string is a deliberate act), so `names.push_back("filename")` compiles unchanged and
// only the places a *runtime* string crosses the boundary need CompatMakeName. Deliberately NOT
// an implicit conversion -- the explicitness is the point of the upstream change.

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
// DuckDB v2.0 enforces a scalar function's declared error mode at runtime: a
// function that throws without having called SetFallible() surfaces as
//   INTERNAL Error: Scalar function "x" threw an execution error, but the
//   function is not marked as fallible
// rather than as the error it threw. Applied at CONSTRUCTION, wrapping the
// ScalarFunction expression, so it is set before the function is added to a
// set -- on v2.0 a set member is shared_ptr<const T> and cannot be configured
// after AddFunction. SetFallible() exists on the pinned 1.5 too, where it only
// informs the optimizer, so there is nothing to guard.
inline ScalarFunction Fallible(ScalarFunction fn) {
	fn.SetFallible();
	return fn;
}

template <typename T>
inline void PreventStructConstantFoldingAndAdd(FunctionSet<T> &func_set, T func) {
	PreventStructConstantFolding(func);
	func_set.AddFunction(std::move(func));
}

} // namespace duckdb
