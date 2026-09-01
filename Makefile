PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=webbed
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile
# Execute every SQL example in docs/ against the built extension.
# Not part of `make test`: 48 examples reference fixture files this does not
# create, so it reports rather than gates. Fails only on examples that break
# against the API we actually ship.
.PHONY: check-docs
check-docs:
	DUCKDB_BIN=./build/release/duckdb python3 scripts/check_doc_sql.py
