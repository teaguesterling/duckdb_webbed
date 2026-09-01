#pragma once

// ============================================================================
// VENDORED COPY -- do not edit by hand without re-syncing from upstream.
//
// Source: teaguesterling/duckdb_duck_block_utils, src/include/duck_block_vocabulary.hpp
// Vendored at upstream commit: b917e77 (SPEC_VERSION 6.2)
//
// Taken from a pinned git object (`git show <sha>:<path>`) against a local
// clone of upstream, NOT from a `raw.githubusercontent.com/.../main/...` URL.
// That branch URL is served from a CDN that is only eventually consistent, so
// a fetch shortly after an upstream push can silently hand back a superseded
// header -- observed in practice by a sibling extension's session. A sha-
// pinned URL (or, as here, a sha-pinned git object) is immutable and cannot
// go stale the same way. Re-vendor the same way: resolve upstream main to a
// commit, then read the header AT that commit, never at the branch.
//
// Vendored (not submoduled) to avoid pulling duck_block_utils' own `duckdb`
// submodule into CI just to deliver this ~12KB header. See
// scripts/check_duck_block_vocabulary.py, which diffs this copy against a
// fresh checkout of upstream to detect drift -- run it (or `make
// check-vocabulary`) after bumping the vendored commit above.

// ============================================================================
// The duck_block vocabulary -- PUBLISHED INTERFACE.
//
// Header-only and link-free ON PURPOSE. Sibling extensions (panduck, webbed,
// duckdb_markdown, sitting_duck) consume this by VENDORING a copy -- see
// "VENDORING THIS FILE" below for why a copy beats a submodule here, and for
// the drift check that has to come with it.
//
// Nothing here has a definition in a .cpp, so including it costs no linking.
// The type constructors and Register() live in block_types.hpp, which needs
// block_types.cpp -- do NOT move them here, or consumers get link errors for
// functions they never called.
//
// Changes to these names are BREAKING for every consumer. Bump SPEC_VERSION
// and say so. Consumers should assert agreement at test time rather than
// trusting that their copy is current:
//
//     SELECT duck_block_kind_names();        -- ['block', 'inline', 'value']
//     SELECT duck_block_type_names();        -- every element_type name
//     SELECT duck_block_spec_version();
//
// See docs/duck_blocks_spec.md for what each name means.
//
// ---------------------------------------------------------------------------
// VENDORING THIS FILE
//
// WHAT ELSE IS VENDORABLE, and where. `vendor/README.md` upstream is the index; this
// header is the one vendorable file that does NOT live there, because four extensions
// already pull it from `src/include/` and moving it to buy tidiness would break their
// tooling for no gain. The others:
//
//     vendor/duck_block_conformance.sql   validity, error detail, declared kind /
//                                         type / encoding lists -- pure DuckDB SQL
//     vendor/duck_block_normalize.hpp     the content rule as a transform
//
// PATHS MOVE. `duck_block_conformance.sql` lived at `conformance/` until 2026-09-01.
// A consumer re-syncing by URL got a 404 -- and duckdb_markdown pointed out how that
// fails: `curl -sS -o file URL` writes GitHub's 404 page INTO the vendored file, and
// `curl -sSf` leaves the stale copy in place while the consumer believes they
// re-synced. Either way they keep validating against an old vocabulary and nothing
// says so.
//
// So CHECK THE HTTP STATUS and fail loudly on anything but 200, the same way you
// would refuse to print OK from an undatable fetch (below). A vendored file that
// silently did not update is the failure this whole block exists to prevent.
//
// Copy it. Do NOT add duck_block_utils as a submodule: the DuckDB extension CI
// templates check out with submodules: 'recursive' and this repo carries its
// own duckdb submodule, so a pin drags a ~290 MB nested DuckDB clone into your
// CI to deliver 12 KB. This file needs none of it -- <cstdint> only.
//
// A submodule pin is ALSO a copy. It sits at whatever sha you checked out and
// never tracks main, so it goes stale exactly as a vendored file does -- and it
// never complains, whereas a check does. It is additionally bounded by the
// upstream PUSH cadence, not the commit cadence: an unpushed commit cannot be
// pinned at all.
//
// So vendor, and add a drift check that actually runs. Note first WHY a check
// is not optional: these constants protect against a RENAME, not against a
// changed VALUE.
//
//     TYPE_PAGE = "page_break"   ->   TYPE_PAGE = "pagebreak"
//
// A rename is a compile error at every use site. That value change compiles
// clean everywhere, and a consumer's writer silently stops matching the type.
// Nothing in C++ catches it -- not a vendored copy, not a submodule pin. Only
// a check does. (Found by the duckdb_markdown session, 2026-08-31.)
//
//   1. Record the provenance where a reader will find it -- in your copy's
//      header comment, note the upstream sha and the SPEC_VERSION below.
//
//   2. Fail your build when a constant is renamed, removed, or CHANGES VALUE.
//      Fetch upstream and compare BY NAME AND VALUE -- parse both sides, do
//      not diff the text:
//
//      DO NOT fetch the /main/ raw url directly. raw.githubusercontent.com serves
//      branch urls from a CDN that is only EVENTUALLY consistent with the branch,
//      so a check running shortly after an upstream push compares against the
//      PREVIOUS version, finds no difference, and reports a clean bill of health.
//      That window is precisely when a drift check matters most, and the failure
//      is in the reassuring direction, which is the direction nobody re-checks.
//      (Found by duckdb_markdown, whose check reported "in sync" against a copy
//      two spec versions old; it only surfaced because a human said otherwise.)
//
//      Resolve the branch to a commit sha first, then fetch the SHA-PINNED url,
//      which is immutable and therefore cannot be stale:
//
//        https://api.github.com/repos/teaguesterling/duckdb_duck_block_utils/commits/main
//        https://raw.githubusercontent.com/teaguesterling/
//          duckdb_duck_block_utils/<sha>/src/include/duck_block_vocabulary.hpp
//
//      Print the sha alongside the verdict, so the output says what it actually
//      compared against. And when the sha lookup fails -- rate limit, outage,
//      offline -- falling back to the branch url is fine, but that path must NEVER
//      print OK: "no drift seen" from a copy you could not date is not a clean
//      bill of health, and reporting it as one is the same defect again.
//
//      A plain `diff` over this file fires on comment edits and cosmetic churn
//      -- commit 3957f36 rewrote every idx_t to uint64_t and changed no name
//      and no value. A check that cries wolf gets muted, and a muted check
//      catches nothing. Silence on cosmetic change is what keeps it credible.
//
//      Worth reporting separately rather than as one pass/fail: DRIFT (renamed,
//      removed, or value changed) should fail; NEW (published here, absent from
//      your copy) and GAPS (published, but nothing in your code branches on it)
//      should report without failing. GAPS is the arm that earns its keep -- it
//      is what surfaced inline `generic` losing source_type in two extensions
//      in the same week. Carry an explicit allowlist of intentional gaps, or an
//      unexplained one and a deliberate one look identical. Filter NUMERIC-valued
//      constants out when selecting the vocabulary by name prefix -- otherwise
//      KIND_IDX and the other struct field offsets get reported as published-but-
//      unhandled types, and a check whose first run on a new consumer is a false
//      positive has already taught that consumer to ignore it. (panduck hit exactly
//      this while adopting the arm from this recommendation.)
//
//      duckdb_markdown has a working implementation of exactly this, which
//      looks in both a vendored and a submodule location:
//      scripts/check_duck_block_vocabulary.py (`make check-vocabulary`).
//
//   3. Assert the RUNTIME vocabulary too, which no file comparison can see --
//      the installed extension may be older than any header:
//
//        SELECT duck_block_type_names();   -- every element_type
//        SELECT duck_block_kind_names();   -- ['block','inline','value']
//        SELECT duck_block_spec_version(); -- major equality + minor floor;
//                                          see SPEC_VERSION below for why not
//                                          plain equality
//
// (2) catches a stale copy; (3) catches a stale INSTALL. They fail differently
// and neither subsumes the other.
// ---------------------------------------------------------------------------
// ============================================================================

// <cstdint> ONLY. This header deliberately pulls in nothing from DuckDB.
//
// It used to include duckdb/common/types.hpp for idx_t, which is `typedef
// uint64_t idx_t` -- so the include bought nothing but a dependency. And it was
// an expensive one: it made this file unusable as a standalone copy, forcing
// consumers toward a submodule -- and the DuckDB extension CI templates check
// out with submodules: 'recursive' while this repo carries its own duckdb
// submodule. That meant a 290 MB nested DuckDB clone in every consumer's CI to
// deliver a 12 KB header.
//
// Field indices are uint64_t, which is type-identical to idx_t.
#include <cstdint>

namespace duckdb {

struct DuckBlockVocabulary {
	// Field indices for duck_block struct
	static constexpr uint64_t KIND_IDX = 0;
	static constexpr uint64_t ELEMENT_TYPE_IDX = 1;
	static constexpr uint64_t CONTENT_IDX = 2;
	static constexpr uint64_t LEVEL_IDX = 3;
	static constexpr uint64_t ENCODING_IDX = 4;
	static constexpr uint64_t ATTRIBUTES_IDX = 5;
	static constexpr uint64_t ELEMENT_ORDER_IDX = 6;

	// Additional field indices for duck_block_ext
	static constexpr uint64_t SOURCE_FORMAT_IDX = 7;
	static constexpr uint64_t FILE_PATH_IDX = 8;

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

	// The duck_block spec version this build implements, as MAJOR.MINOR.
	//
	//   MAJOR bumps for a BREAKING change -- a shape or vocabulary change that a
	//         conforming consumer must migrate for.
	//   MINOR bumps for an ADDITIVE one -- new types or attributes that existing
	//         consumers can ignore.
	//
	// ASSERT MAJOR EQUALITY AND A MINOR FLOOR, not equality on the whole string.
	// Equality goes red on every release including ones that cannot affect you,
	// and a check that cries wolf gets muted:
	//
	//     major(duck_block_spec_version()) == 2   AND   minor(...) >= <what you need>
	//
	// (Asked by panduck, who noticed the guidance said "compare against
	// SPEC_VERSION" without saying compare HOW, and whose readers are untouched by
	// 2.0 apart from lists.)
	//
	// HONEST HISTORY, because the numbers only mean something if they were applied
	// consistently and one of these was not:
	//
	//   1.1 -> 1.2  list and blockquote became structural. BREAKING -- it broke
	//               duckdb_markdown's writer in three places. It should have been
	//               2.0 and the minor bump was wrong. A consumer pinning "major 1"
	//               would have been broken by a release the numbering promised was
	//               safe.
	//   1.2 -> 2.0  one shape per BLOCK element_type. Breaking, numbered correctly.
	//   2.0 -> 3.0  every element carries an EXPLICIT level; no NULLs, one scale for
	//               blocks and inlines, and `level` is never semantic. Breaking.
	//               The NULL-at-top-level convention 1.x and 2.0 documented was
	//               never approved -- see docs/duck_blocks_spec.md.
	//   3.0 -> 4.0  `plain` added: Pandoc's Plain constructor, which this reader had
	//               been collapsing onto `paragraph`, losing the tight vs loose list
	//               distinction. BREAKING by this contract's own rule -- a consumer
	//               rendering `paragraph` now receives `plain` for a tight list item
	//               -- so a MAJOR bump even though the vocabulary change is additive.
	//               Also makes `list_type` canonical, `ordered` a legacy alias.
	//   4.0 -> 5.0  `table` emits the NATIVE {headers,rows} schema, with the full
	//               Pandoc tuple preserved in attributes['pandoc_ast'] so nothing is
	//               lost; definition lists become `list` with list_type='definition'
	//               rather than the opaque `deflist`. Both previously serialised
	//               structure into a field consumers read verbatim: tables rendered
	//               as NOTHING and deflists rendered their own AST, and both poisoned
	//               search. Breaking -- a consumer parsing either JSON must migrate.
	//
	//   5.0 -> 6.0  `plain` is NARROWED to text that has nowhere else to live. A
	//               container whose only child is a text run carries that text in its
	//               own `content` -- v1's content rule, unchanged since v1.0 -- so
	//               `<li>text</li>` is `list_item(content='text')`, not
	//               `list_item > plain('text')`. 5.0 shipped the second, which meant a
	//               container with a single text child had TWO legal shapes depending
	//               on which producer built it, and removing that ambiguity is the
	//               thing every version since 2.0 has been for.
	//
	//               `plain` still exists and is still required, in exactly two places:
	//                 * beside block siblings -- `section > plain('Lead') + heading`,
	//                   where the container's content cannot hold the run because the
	//                   run is not the only child;
	//                 * at the TOP LEVEL, where the document root has no content field.
	//
	//               Tight-vs-loose list items are NOT lost by this and need no
	//               attribute: content on the item is Pandoc's `Plain` (tight), a
	//               `paragraph` child is `Para` (loose). Measured on the exporter
	//               before the change, not assumed.
	//
	//               Breaking for a consumer that walks for a `plain` child; a consumer
	//               that already read the container's `content` -- which the rule has
	//               required since v1 -- needs no change.
	//
	//   6.0 -> 6.1  ADDITIVE. `duck_blocks_normalize(blocks)` applies 6.0's content
	//               rule to a finished block vector, so a producer can emit the naive
	//               shape and fix it up afterwards instead of implementing the rule.
	//
	//               Needed because the rule is SIBLING-DEPENDENT: whether a text run
	//               becomes its container's content or stays a `plain` depends on what
	//               FOLLOWS it, which a streaming reader does not know when it reaches
	//               the run. Raised by the panduck session, whose EPUB and LaTeX
	//               readers both emit as they walk; it is true of any streaming reader
	//               of any format, so the answer should not be four private lookaheads
	//               that drift. Nothing is removed or renamed -- a consumer on 6.0 is
	//               unaffected.
	//
	//   6.1 -> 6.2  ADDITIVE, three clarifications that were load-bearing and unstated.
	//               `metadata` gains attributes['role'], with 'frontmatter' declared --
	//               one type plus a role rather than minting `frontmatter` as its own
	//               element_type, which is this vocabulary's own rule applied to a case
	//               three producers had each guessed differently.
	//
	//               Value elements after the blocks is now a CONTRACT, not the
	//               "convenience" the spec called it: two producers asked where they go,
	//               which is a question a convenience cannot answer. With it, the rule
	//               that a consumer must end an inline run at any NON-INLINE element --
	//               getting that wrong made this repo's exporter emit a document whose
	//               body had been replaced by its title.
	//
	//               A document with NO blocks -- a .toml file read as pure metadata --
	//               is conformant, and stated rather than left to be inferred from
	//               nothing objecting.
	//
	//               Nothing renamed, nothing removed; a consumer on 6.1 is unaffected.
	//
	// The rule above is what will be followed from here.
	static constexpr const char *SPEC_VERSION = "6.2";

	// ========================================================================
	// Block type names
	// ========================================================================
	static constexpr const char *TYPE_HEADING = "heading";
	static constexpr const char *TYPE_PARAGRAPH = "paragraph";
	// A block-level text run with NO paragraph semantics -- Pandoc's `Plain`, and
	// HTML text that is not wrapped in a <p>: `<li>text</li>`, `<td>text</td>`,
	// `<dd>text</dd>`, `<figcaption>text</figcaption>`.
	//
	// This existed in Pandoc all along and this reader collapsed it onto
	// `paragraph`, which is how the TIGHT vs LOOSE list distinction was being lost
	// -- and lost independently in webbed, by a different mechanism, with neither
	// reader aware. Modelling it as its own type rather than an attribute keeps the
	// mapping honest: it is a constructor we were failing to represent, not a
	// variation we were failing to annotate.
	static constexpr const char *TYPE_PLAIN = "plain";
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
	// TOML is a distinct syntax with no equivalent in the set above. Emitting a
	// verbatim .toml blob as 'text' discards the one fact a consumer needs in order to
	// parse it, which is the opposite of what a verbatim blob is for. Asked for by the
	// panduck session rather than minted by them -- the behaviour the spec asks for.
	static constexpr const char *ENCODING_TOML = "toml";

	// ========================================================================
	// Attribute NAMES
	//
	// These were bare string literals until 2026-09-01 -- `"role"` appeared in six
	// places in this repo's own source and in every consumer's, and a drift check that
	// compares CONSTANTS by name and value could see none of it. A misspelled `"roles"`
	// produces an element that is valid, conformant, lint-clean and wrong.
	//
	// Raised by duckdb_markdown, and the argument is the one that decided
	// `metadata`+role over minting a `frontmatter` type in the first place: if the ROLE
	// carries the meaning, the role needs the enforcement the type has.
	// ========================================================================
	static constexpr const char *ATTR_ROLE = "role";
	static constexpr const char *ATTR_KEY = "key";
	static constexpr const char *ATTR_HEADING_LEVEL = "heading_level";
	static constexpr const char *ATTR_LIST_TYPE = "list_type";
	static constexpr const char *ATTR_SOURCE_TYPE = "source_type";
	static constexpr const char *ATTR_PANDOC_AST = "pandoc_ast";

	// ========================================================================
	// Role values, per the type that carries them
	// ========================================================================
	// `metadata` -- which verbatim blob this is
	static constexpr const char *ROLE_FRONTMATTER = "frontmatter"; // has a document body after it
	static constexpr const char *ROLE_DOCUMENT = "document";       // the blob IS the whole document

	// ========================================================================
	// `list_type` values -- the attribute is ATTR_LIST_TYPE
	//
	// The KEY got a constant and the VALUES did not, which duckdb_markdown named as the
	// shape that keeps recurring: "the key gets a constant, the value it is compared
	// against does not." Their writer branches on `list_type == "definition"` to choose
	// the definition-list rendering, so a value rename upstream turns every definition
	// list into a bullet list, silently, with every check green.
	//
	// WHY `list_type` AND NOT THE OLDER `attributes['ordered']`. A boolean answers
	// "ordered or not" and cannot answer "which KIND of list", so it had no way to say
	// `definition` -- and definition lists arrived the same day the question was
	// settled, needing zero new element_types because `list_type` could carry them.
	// A `navlist`, or anything else with list semantics, lands the same way.
	//
	// That is the general rule and it is worth more than this instance: WHEN
	// COLLAPSING TWO NAMES FOR ONE FACT, KEEP THE ONE THAT CAN STILL EXPRESS THE FACT
	// WHEN IT GROWS. The v1-fidelity argument said keep `ordered`; it optimised for
	// matching the past and never asked what the next value would need.
	//
	// `attributes['ordered']` = 'true'/'false' remains a LEGACY ALIAS. Producers may
	// emit both -- this one does -- and CONSUMERS MUST TOLERATE IT, because data
	// written before the rule exists and does not rewrite itself. Read `list_type`
	// first and fall back.
	//
	// Recorded here rather than only in the spec because a consumer reading these
	// constants is exactly the reader who needs it, and would otherwise not learn the
	// alias exists at all. (duckeye's point: a reason like this belongs beside the
	// constant, not only in the thread that produced it.)
	// ========================================================================
	static constexpr const char *LIST_TYPE_BULLET = "bullet";
	static constexpr const char *LIST_TYPE_ORDERED = "ordered";
	static constexpr const char *LIST_TYPE_DEFINITION = "definition";
	// The legacy boolean. Declared so a consumer tolerating it names the attribute
	// rather than spelling it, and so a drift check can see it at all.
	static constexpr const char *ATTR_ORDERED_LEGACY = "ordered";

	// `list_item` in a definition list
	static constexpr const char *ROLE_TERM = "term";
	static constexpr const char *ROLE_DEFINITION = "definition";

	// `section` -- which sectioning construct
	static constexpr const char *ROLE_SECTION = "section";
	static constexpr const char *ROLE_ARTICLE = "article";
	static constexpr const char *ROLE_ASIDE = "aside";
	static constexpr const char *ROLE_NAV = "nav";
	static constexpr const char *ROLE_HEADER = "header";
	static constexpr const char *ROLE_FOOTER = "footer";
	static constexpr const char *ROLE_MAIN = "main";
	static constexpr const char *ROLE_PAGE = "page";
};

} // namespace duckdb
