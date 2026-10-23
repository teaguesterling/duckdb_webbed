#pragma once

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parallel/pipeline.hpp"

namespace duckdb {

// Batch index layout for multi-file readers that preserve file order: high bits = file index
// (glob order), low bits = chunk within the file, so indices are strictly ordered by (file, chunk).
// DuckDB rejects source batch indices that reach PipelineBuildState::BATCH_INCREMENT (10^13, about
// 2^43), so both fields share that budget: 2^20 chunks per file (about 2 billion rows at 2048 rows
// per chunk) and about 9.5 million files.
struct FileBatchIndex {
	static constexpr idx_t CHUNK_BITS = 20;
	static constexpr idx_t MAX_CHUNKS_PER_FILE = idx_t(1) << CHUNK_BITS;
	// The pipeline maps source index i to base + i + 1 and requires it to stay below
	// base + BATCH_INCREMENT - 1, so source indices must stay below BATCH_INCREMENT - 2.
	static constexpr idx_t MAX_FILES = (PipelineBuildState::BATCH_INCREMENT - 2) >> CHUNK_BITS;
	static_assert((MAX_FILES << CHUNK_BITS) <= PipelineBuildState::BATCH_INCREMENT - 2,
	              "largest batch index must stay within the pipeline's batch index range");

	// Throws a user-facing error when (file_index, chunk_index) does not fit the layout, which
	// would otherwise surface as DuckDB's internal "invalid batch index" error. The exception
	// constructor binds its arguments by reference, so the limits are passed as copies: before C++17
	// a static constexpr member has no out-of-line definition to bind to.
	static void Validate(idx_t file_index, idx_t chunk_index, const string &filename) {
		if (file_index >= MAX_FILES) {
			throw InvalidInputException("Cannot read more than %llu files while preserving insertion order. Run SET "
			                            "preserve_insertion_order = false to read them without preserving file order.",
			                            idx_t(MAX_FILES));
		}
		if (chunk_index >= MAX_CHUNKS_PER_FILE) {
			throw InvalidInputException(
			    "File \"%s\" produced more than %llu chunks, the most a read that preserves insertion order "
			    "supports per file. Run SET preserve_insertion_order = false to read it without preserving "
			    "file order.",
			    filename, idx_t(MAX_CHUNKS_PER_FILE));
		}
	}
};

} // namespace duckdb
