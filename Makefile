PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=webbed
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile
# Execute every SQL example in docs/ against the built extension. Sample
# documents live in test/docs_fixtures/, so examples that read files resolve
# the way they do for a reader who created them.
#
# Fails only on examples that break against the API we actually ship: a missing
# fixture file or table is reported, but a missing function or an unresolved
# column is an error, because that means the docs describe something we do not
# have. Needs a release build; run `make release` first, or point DUCKDB_BIN at
# another binary.
.PHONY: check-docs
check-docs:
	DUCKDB_BIN=./build/release/duckdb python3 scripts/check_doc_sql.py

# Run the documentation examples as part of `make test`. Listed after the
# ci-tools `test: test_release` rule, so the unit tests run first.
test: check-docs
