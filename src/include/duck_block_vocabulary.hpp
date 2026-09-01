#pragma once

// ============================================================================
// VENDORED COPY -- do not edit by hand without re-syncing from upstream.
//
// Source: teaguesterling/duckdb_duck_block_utils, src/include/duck_block_vocabulary.hpp
// Vendored at upstream commit: e44efe61f096fb462bd02aa09afdbf0caaf1f70a
//
// Vendored (not submoduled) to avoid pulling duck_block_utils' own `duckdb`
// submodule into CI just to deliver this ~12KB header. See
// scripts/check_duck_block_vocabulary.py, which diffs this copy against a
// fresh checkout of upstream to detect drift -- run it (or `make
// check-vocabulary`) after bumping the vendored commit above.
// ============================================================================


// ============================================================================
// The duck_block vocabulary -- PUBLISHED INTERFACE.
//
// Header-only and link-free ON PURPOSE. Sibling extensions (panduck, webbed,
// duckdb_markdown, sitting_duck) are expected to consume this via a submodule
// rather than copying it; copies drift silently, which is exactly the defect
// this file exists to prevent.
//
// Nothing here has a definition in a .cpp, so including it costs no linking.
// The type constructors and Register() live in block_types.hpp, which needs
// block_types.cpp -- do NOT move them here, or consumers get link errors for
// functions they never called.
//
// Changes to these names are BREAKING for every consumer. Bump SPEC_VERSION
// and say so. Consumers should assert agreement at test time rather than
// trusting a synced submodule:
//
//     SELECT duck_block_kind_names();        -- ['block', 'inline', 'value']
//     SELECT duck_block_type_names();        -- every element_type name
//     SELECT duck_block_spec_version();
//
// See docs/duck_blocks_spec.md for what each name means.
// ============================================================================

#include "duckdb/common/types.hpp"

namespace duckdb {

struct DuckBlockVocabulary {
	// Field indices for duck_block struct
	static constexpr idx_t KIND_IDX = 0;
	static constexpr idx_t ELEMENT_TYPE_IDX = 1;
	static constexpr idx_t CONTENT_IDX = 2;
	static constexpr idx_t LEVEL_IDX = 3;
	static constexpr idx_t ENCODING_IDX = 4;
	static constexpr idx_t ATTRIBUTES_IDX = 5;
	static constexpr idx_t ELEMENT_ORDER_IDX = 6;

	// Additional field indices for duck_block_ext
	static constexpr idx_t SOURCE_FORMAT_IDX = 7;
	static constexpr idx_t FILE_PATH_IDX = 8;

	// Kind values
	static constexpr const char *KIND_BLOCK = "block";
	static constexpr const char *KIND_INLINE = "inline";
	// Non-prose data attached to a document -- currently its metadata. Consumers that
	// walk document content filter on KIND_BLOCK and ignore these automatically, which
	// is what makes the kind additive rather than breaking.
	static constexpr const char *KIND_VALUE = "value";

	// ========================================================================
	// Value type names (kind = 'value')
	// ========================================================================
	// Model Pandoc's recursive MetaValue tree. `list` and `map` nest their children
	// via `level`, exactly as `div` and `figure` do; `inlines` and `blocks` carry
	// ordinary kind='inline'/'block' children. The map key, where there is one, is in
	// attributes['key'].
	static constexpr const char *VALUE_STRING = "string";
	static constexpr const char *VALUE_BOOL = "bool";
	static constexpr const char *VALUE_LIST = "list";
	static constexpr const char *VALUE_MAP = "map";
	static constexpr const char *VALUE_INLINES = "inlines";
	static constexpr const char *VALUE_BLOCKS = "blocks";
	// Stream metadata, not document metadata: records which duck_block spec a
	// persisted or exchanged block list was written against. Carries no
	// attributes['key'], which is what keeps it out of a document's Pandoc `meta`.
	static constexpr const char *VALUE_VERSION = "version";

	// The duck_block spec version this build implements. Bump when the vocabulary
	// changes in a way a consumer could observe.
	static constexpr const char *SPEC_VERSION = "1.1";

	// ========================================================================
	// Block type names
	// ========================================================================
	static constexpr const char *TYPE_HEADING = "heading";
	static constexpr const char *TYPE_PARAGRAPH = "paragraph";
	static constexpr const char *TYPE_CODE = "code";
	static constexpr const char *TYPE_BLOCKQUOTE = "blockquote";
	static constexpr const char *TYPE_LIST = "list";
	static constexpr const char *TYPE_LIST_ITEM = "list_item";
	static constexpr const char *TYPE_TABLE = "table";
	static constexpr const char *TYPE_HR = "hr";
	static constexpr const char *TYPE_METADATA = "metadata";
	static constexpr const char *TYPE_IMAGE = "image";
	static constexpr const char *TYPE_RAW = "raw";
	static constexpr const char *TYPE_DIV = "div";
	// A SEMANTIC sectioning container, distinct from `div`. HTML's own spec calls
	// div "an element of last resort", so mapping <section>/<article>/<aside> onto
	// it would make element_type say something false while the truth hid in an
	// attribute. Which kind of section lives in attributes['role'] -- section,
	// article, aside, nav, header, footer, main -- following the convention already
	// set by heading+heading_level, list+list_type and quoted+quote_type rather
	// than minting one type per variant.
	// Pandoc has no Section constructor, so this exports as a Div whose class is
	// the role; that is pandoc's nearest honest equivalent.
	static constexpr const char *TYPE_SECTION = "section";
	// A physical pagination boundary -- a MARKER, not a container. Like `hr` it
	// carries no content and owns no children; element_order already groups
	// "blocks between marker N and N+1", so a container would re-nest whole
	// documents for nothing. The number lives in attributes['page_number'].
	//
	// Physical, NOT semantic: a table of contents and a section slicer must
	// IGNORE pages, which is precisely what they cannot do when a reader fakes
	// them as headings.
	static constexpr const char *TYPE_PAGE = "page_break";
	static constexpr const char *TYPE_LINEBLOCK = "lineblock";
	static constexpr const char *TYPE_DEFLIST = "deflist";
	static constexpr const char *TYPE_FIGURE = "figure";
	// A caption belonging to the container that precedes it. Deliberately general
	// rather than figure-specific: Pandoc's Table also carries a Caption, and can
	// adopt this later without another vocabulary change.
	static constexpr const char *TYPE_CAPTION = "caption";
	// A structurally-valid element whose type is not in the standard vocabulary.
	// Distinct from TYPE_RAW, which is literal content in a *named* format; this is a
	// structured element we cannot name. Format-neutral on purpose: any reader
	// (pandoc, html, sitting_duck) can use it. The originating type name is preserved
	// in attributes['source_type']. Shares its string with INLINE_GENERIC; `kind`
	// disambiguates, as it already does for code/image/raw.
	static constexpr const char *TYPE_GENERIC = "generic";

	// ========================================================================
	// Inline type names
	// ========================================================================
	// Text and whitespace
	static constexpr const char *INLINE_TEXT = "text";
	static constexpr const char *INLINE_SPACE = "space";
	static constexpr const char *INLINE_SOFTBREAK = "softbreak";
	static constexpr const char *INLINE_LINEBREAK = "linebreak";

	// Formatting (container types)
	static constexpr const char *INLINE_BOLD = "bold";
	static constexpr const char *INLINE_ITALIC = "italic";
	static constexpr const char *INLINE_STRIKETHROUGH = "strikethrough";
	static constexpr const char *INLINE_SUPERSCRIPT = "superscript";
	static constexpr const char *INLINE_SUBSCRIPT = "subscript";
	static constexpr const char *INLINE_SMALLCAPS = "smallcaps";
	static constexpr const char *INLINE_UNDERLINE = "underline";

	// Semantic
	static constexpr const char *INLINE_CODE = "code";
	static constexpr const char *INLINE_MATH = "math";
	static constexpr const char *INLINE_LINK = "link";
	static constexpr const char *INLINE_IMAGE = "image";
	static constexpr const char *INLINE_QUOTED = "quoted";
	static constexpr const char *INLINE_CITE = "cite";
	static constexpr const char *INLINE_NOTE = "note";
	static constexpr const char *INLINE_SPAN = "span";
	static constexpr const char *INLINE_RAW = "raw";
	// Inline counterpart of TYPE_GENERIC -- same string, distinguished by kind.
	static constexpr const char *INLINE_GENERIC = "generic";

	// ========================================================================
	// Encoding values
	// ========================================================================
	static constexpr const char *ENCODING_TEXT = "text";
	static constexpr const char *ENCODING_JSON = "json";
	static constexpr const char *ENCODING_YAML = "yaml";
	static constexpr const char *ENCODING_HTML = "html";
	static constexpr const char *ENCODING_XML = "xml";
	static constexpr const char *ENCODING_LATEX = "latex";
	static constexpr const char *ENCODING_MARKDOWN = "markdown";
};

} // namespace duckdb
