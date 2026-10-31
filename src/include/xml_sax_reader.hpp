#pragma once

#include "duckdb.hpp"
#include "xml_schema_inference.hpp"
#include "duckdb/common/file_system.hpp"
#include <libxml/parser.h>
#include <string>
#include <vector>

namespace duckdb {

// Streams an XML file through libxml2's tree builder, building only record subtrees and their
// ancestors.
class XMLRecordStream {
public:
	// keep_records leaves completed records in the document for TakeDocument. max_records > 0 stops
	// the parse once that many records complete. prefix holds bytes already read from file, parsed first.
	XMLRecordStream(std::unique_ptr<FileHandle> file, const std::string &filename, const XMLSchemaOptions &options,
	                idx_t max_records = 0, bool keep_records = false, std::string prefix = std::string());
	~XMLRecordStream();

	XMLRecordStream(const XMLRecordStream &) = delete;
	XMLRecordStream &operator=(const XMLRecordStream &) = delete;

	// Parse the next chunk of the file, adding any records it completes to Records(). Returns false
	// once the document ends or max_records is reached. Throws on malformed XML.
	bool ParseChunk();

	const std::vector<xmlNodePtr> &Records() const {
		return state.records;
	}

	// Free the records returned so far, along with finished elements outside any record.
	void ReleaseRecords();

	// Hand over the document holding the kept records. Valid once parsing stops.
	xmlDocPtr TakeDocument();

	static std::string DescribeParseError(xmlParserCtxtPtr ctx);

	// Callback state, public for the libxml2 handlers in the implementation file.
	struct State {
		std::string record_tag;
		std::string root_element;
		bool root_is_record = false;
		bool keep_prefix = false;
		bool keep_records = false;
		idx_t max_records = 0;
		idx_t max_record_bytes = 0;
		// Input bytes handed to the parser before the current chunk, and that count when the open record started.
		idx_t fed_bytes = 0;
		idx_t record_start_bytes = 0;

		int depth = 0;
		int record_depth = 0;
		int root_depth = 0;
		bool root_done = false;
		bool stopped = false;
		idx_t records_completed = 0;
		// Per open element: whether a record started inside it, so keep_records leaves it in place.
		std::vector<bool> holds_record;

		std::vector<xmlNodePtr> records;
		std::vector<xmlNodePtr> garbage;
	};

private:
	static constexpr idx_t CHUNK_SIZE = 65536;

	std::string filename;
	std::unique_ptr<FileHandle> file;
	xmlSAXHandler handler;
	xmlParserCtxtPtr ctxt = nullptr;
	State state;
	std::vector<char> buffer;
	std::string prefix;
	idx_t prefix_offset = 0;
	bool finished = false;
};

class SAXStreamReader {
public:
	// Infer the schema from the first sample_size records.
	static std::vector<XMLColumnInfo> InferSchemaFromStream(FileSystem &fs, const std::string &filename,
	                                                        const XMLSchemaOptions &options);

	// Opt-in second bind-time pass (sample_size <= 0): stream the whole file and widen to VARCHAR any
	// column whose out-of-sample values don't cast to the sampled type, and append child elements first
	// seen past the sample. Memory stays bounded by one chunk of records.
	static void WidenSchemaForOutOfSampleValues(FileSystem &fs, const std::string &filename,
	                                            const XMLSchemaOptions &options, std::vector<XMLColumnInfo> &columns);
};

} // namespace duckdb
