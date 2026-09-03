PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=webbed
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Check the vendored duck_block vocabulary (src/include/duck_block_vocabulary.hpp)
# against upstream, by NAME AND VALUE. A vendored copy does not notice when upstream
# moves; more importantly, the C++ constants catch a RENAME (compile error) but not a
# changed VALUE, which compiles clean and silently stops matching. Fails (exit 1) on
# drift. Prefers a local clone of duck_block_utils over a network fetch when one is
# found next to this checkout -- a network fetch is a stale-read failure mode with no
# local symptom (raw.githubusercontent.com serves branch urls from an eventually
# consistent cache); set UPSTREAM_DUCK_BLOCK_UTILS to point at a different clone.
# Falls back to fetching the published header over HTTPS when no local clone is found,
# same as the script's own default.
UPSTREAM_DUCK_BLOCK_UTILS ?= $(HOME)/Projects/duckdb_duck_block_utils
.PHONY: check-vocabulary
check-vocabulary:
	python3 scripts/check_duck_block_vocabulary.py \
		$(if $(wildcard $(UPSTREAM_DUCK_BLOCK_UTILS)/.git),--upstream $(UPSTREAM_DUCK_BLOCK_UTILS),)

# Execute every SQL example in docs/ against the built extension. Sample
# documents live in test/docs_fixtures/, so examples that read files resolve
# the way they do for a reader who created them.
#
# Not wired into `make test`. Fails only on examples that break against the
# API we actually ship: a missing fixture file or table is reported, but a
# missing function or an unresolved column is an error, because that means
# the docs describe something we do not have. Needs a release build; run
# `make release` first, or point DUCKDB_BIN at another binary.
.PHONY: check-docs
check-docs:
	DUCKDB_BIN=./build/release/duckdb python3 scripts/check_doc_sql.py
