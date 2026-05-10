#pragma once

#include "duckdb.hpp"

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

namespace duckdb {

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

// --- Constant folding workaround ---
// DuckDB main's VectorStructBuffer::SetVectorType throws InternalException when
// the optimizer constant-folds functions returning STRUCT-containing types
// (LIST(STRUCT), STRUCT, MAP). Mark them VOLATILE to skip constant folding.
inline void PreventStructConstantFolding(ScalarFunction &func) {
	func.SetStability(FunctionStability::VOLATILE);
}
template <typename T>
inline void PreventStructConstantFolding(FunctionSet<T> &func_set) {
	for (auto &func : func_set.functions) {
		func.SetStability(FunctionStability::VOLATILE);
	}
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

// No-op on old API — constant folding works fine for complex types
inline void PreventStructConstantFolding(ScalarFunction &) {
}
template <typename T>
inline void PreventStructConstantFolding(FunctionSet<T> &) {
}

#endif

} // namespace duckdb
