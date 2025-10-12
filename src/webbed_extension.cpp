#define DUCKDB_EXTENSION_MAIN

#include "webbed_extension.hpp"
#include "xml_types.hpp"
#include "xml_scalar_functions.hpp"
#include "xml_reader_functions.hpp"
#include "xml_utils.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/config.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// JSON extension is automatically available as a dependency

	// Initialize libxml2
	XMLUtils::InitializeLibXML();

	// Register XML types (includes JSON to XML casting)
	XMLTypes::Register(loader);

	// Register scalar functions
	XMLScalarFunctions::Register(loader);

	// Register table functions
	XMLReaderFunctions::Register(loader);

	// Register replacement scan for direct file querying (FROM 'file.xml')
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.replacement_scans.emplace_back(XMLReaderFunctions::ReadXMLReplacement);
}

void WebbedExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

// Add cleanup when extension is unloaded
static void UnloadInternal() {
	XMLUtils::CleanupLibXML();
}

std::string WebbedExtension::Name() {
	return "webbed";
}

std::string WebbedExtension::Version() const {
#ifdef EXT_VERSION_WEBBED
	return EXT_VERSION_WEBBED;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(webbed, loader) {
	duckdb::LoadInternal(loader);
}

DUCKDB_EXTENSION_API const char *webbed_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
