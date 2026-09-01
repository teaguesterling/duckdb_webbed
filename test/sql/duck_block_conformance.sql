-- ============================================================================
-- VENDORED COPY -- do not edit by hand without re-syncing from upstream.
--
-- Source: teaguesterling/duckdb_duck_block_utils, vendor/duck_block_conformance.sql
-- Vendored at upstream commit: 143341f
--
-- This is an intentional COPY, not a load of the real thing. webbed builds its
-- own DuckDB from a pinned submodule reporting a dev version string (e.g.
-- 'v1.5.5-dev154'), and DuckDB matches extension ABI by EXACT version string --
-- so webbed structurally CANNOT LOAD the duck_block_utils extension (not
-- INSTALL, not LOAD '<path>') to get duck_blocks_are_valid() the normal way.
-- This file is pure SQL with no extension dependency, published upstream
-- specifically so a consumer in this position can vendor it instead. See the
-- upstream file's own header comment (preserved below) for the full rationale
-- and for USAGE, including why a table-function producer must be materialised
-- into a temp table before calling duck_blocks_are_valid() on it.
--
-- Taken from a pinned git object (`git show <sha>:<path>`), not a `/main/`
-- raw URL, for the same CDN-staleness reason src/include/duck_block_vocabulary.hpp
-- documents.
-- ============================================================================

-- duck_block conformance checks, PURE SQL -- no duck_block_utils required.
--
-- WHY THIS FILE EXISTS. duck_blocks_validate() ships inside an extension built for a
-- specific DuckDB version, and DuckDB matches extension ABI by EXACT version string.
-- An extension that vendors its own DuckDB submodule pinned off-release reports e.g.
-- 'v1.5.5-dev154' and CANNOT LOAD duck_block_utils by any route -- not INSTALL, not
-- LOAD '<path>', and publishing would not change it.
--
-- Raised by the webbed session, whose metadata blocks carried a NULL level for three
-- major spec versions with nothing in a position to object: the check that would have
-- caught it was one they structurally could not run. If markdown and panduck vendor
-- duckdb too, none of the three consuming extensions could run the validator, which
-- explains a great deal about how long these defects survived.
--
-- So conformance for that class of consumer has to be SHAPE-BASED, not
-- extension-based. Copy this file; it needs nothing but DuckDB.
--
-- It is kept honest by test/check_conformance_macro.py, which runs both these macros
-- and the real duck_blocks_validate() over the same corpus and FAILS if they disagree.
-- Two copies of one rule checked by the same party cannot detect their own
-- disagreement -- this repo shipped exactly that defect in an image's alt text -- so
-- the two implementations are compared rather than merely both maintained.

-- WHAT THIS BUYS YOU, from the panduck session and sharper than the framing above:
-- the value is not "the same checks without a dependency". It is TWO CHECKS YOU COULD
-- NOT HAVE HAD AT ALL. Duplicate element_order and level-jumping-by-more-than-one are
-- inexpressible in a per-element macro, and the second is the rule whose absence caused
-- a year of drift across four extensions -- so every consumer who copied the
-- per-element macro this spec published until 2026-09-01 was unguarded against
-- precisely the defect most likely to bite them.

-- USAGE, and read this before the first call fails.
--
--   SELECT duck_blocks_are_valid(<a LIST(duck_block) expression>);
--
-- If your producer is a TABLE function, DuckDB rejects passing it inline:
--
--   SELECT duck_blocks_are_valid(my_reader('doc.epub'));
--     -> Binder Error: Table function cannot contain subqueries
--
-- Materialise it first:
--
--   CREATE TEMP TABLE b AS SELECT my_reader('doc.epub') AS blk;
--   SELECT duck_blocks_are_valid(blk) FROM b;
--
-- MATERIALISE FIRST, ALWAYS. Do not try to work out whether your producer is exempt.
--
-- This file said "a scalar producer passes inline fine; the restriction is specific to
-- table functions". That was measured -- and measured on THIS repo's producers, which
-- are the ones that happen to work. panduck's producer is also scalar and does NOT
-- work, because what trips the binder is what the producer EXPANDS TO once inlined
-- into a subquery position, not the kind it is declared as. panduck's dispatch runs
-- through `query()`, which rejects a subquery in its argument including one that
-- arrives by macro inlining.
--
-- So the exemption is not knowable from here: it depends on an implementation this
-- file cannot see. An accurate-sounding exemption is worse than none, because it
-- invites exactly the inline call that fails, with an error naming subqueries the
-- caller never wrote.
--
-- Reported twice by the panduck session: once as the restriction, once to correct the
-- exemption I wrote in response to it.
--
-- The macros are `duck_block_is_valid` (one element) and `duck_blocks_are_valid` (a
-- document). The FILE name is not a macro name -- also panduck, who grepped for it.

-- Per-element shape. Covers 4 of the 6 things duck_blocks_validate reports.
-- Declared `kind` values. A LITERAL here until 2026-09-01, which is how the version of
-- this check published in the spec came to enumerate two kinds when three exist -- the
-- instruction that leaves a producer with nowhere to put a document's title. It is a
-- macro now for the same reason the type list is: check_conformance_macro.py compares
-- it against duck_block_kind_names() and FAILS on drift, so the copy cannot rot.
--
-- Kinds and element types are DIFFERENT AXES and both are needed. duckdb_markdown
-- reported a false positive from comparing the type list against the kind names; the
-- file offered only one of the two, so the mistake was available to make.
CREATE OR REPLACE MACRO duck_block_declared_kinds() AS (['block', 'inline', 'value']);

CREATE OR REPLACE MACRO duck_block_is_valid(elem) AS (
    list_contains(duck_block_declared_kinds(), elem.kind)
    AND elem.element_type IS NOT NULL
    AND elem.encoding IN ('text', 'json', 'yaml', 'html', 'xml', 'latex', 'markdown')
    AND elem.level IS NOT NULL
    AND elem.level >= 1
    AND elem.element_order IS NOT NULL
    AND elem.element_order >= 0
);

-- Declared element_type names, for the check the EXTENSION does and this file could
-- not. An element_type outside the vocabulary was invisible to everything until
-- 2026-09-01: `duck_blocks_validate` accepted any string. duckdb_markdown emits
-- `frontmatter` where the vocabulary declares `metadata` and nothing objected.
--
-- This list is a COPY of duck_block_type_names(), which is the kind of thing that
-- drifts. test/check_conformance_macro.py compares the two on every `make check` and
-- FAILS if they differ, so it is a copy that cannot rot silently -- the same reason the
-- macros above are compared against the real validator rather than merely maintained.
--
-- Kept SEPARATE from duck_blocks_are_valid deliberately: an unknown type is a LINT, not
-- an invalidity. A consumer built against an older vocabulary must still read data from
-- a newer producer, so a name you do not recognise is reported, not refused. Rejecting
-- it would make every additive spec release break every consumer who had not upgraded.
CREATE OR REPLACE MACRO duck_block_declared_types() AS (
    [
        'blockquote', 'blocks', 'bold', 'bool', 'caption', 'cite', 'code', 'deflist',
        'div', 'figure', 'generic', 'heading', 'hr', 'image', 'inlines', 'italic',
        'lineblock', 'linebreak', 'link', 'list', 'list_item', 'map', 'math', 'metadata',
        'note', 'page_break', 'paragraph', 'plain', 'quoted', 'raw', 'section',
        'smallcaps', 'softbreak', 'space', 'span', 'strikethrough', 'string', 'subscript',
        'superscript', 'table', 'text', 'underline', 'version'
    ]
);

-- Which elements carry a type this vocabulary does not declare. Empty is conforming;
-- anything listed should probably be `generic` with the original name kept in
-- attributes['source_type'], so it reads as a gap rather than a private invention.
CREATE OR REPLACE MACRO duck_blocks_undeclared_types(blocks) AS (
    -- coalesce, because an aggregate over ZERO rows returns NULL, and a conforming
    -- document has zero rows here. NULL is the one answer this must never give: it
    -- reads as "could not tell" and as "none" equally, which is the ambiguity every
    -- check in this file exists to remove. Caught by running it, not by writing it.
    coalesce(
        (SELECT list_sort(list_distinct(list(e.element_type)))
         FROM unnest(blocks) AS t(e)
         WHERE NOT list_contains(duck_block_declared_types(), e.element_type)),
        []::VARCHAR[]
    )
);

-- ERRORS WITH DETAIL, not just a verdict. Requested by Teague after panduck hit the
-- gap immediately: `duck_blocks_are_valid` tells you a fixture broke and nothing about
-- WHERE, so they were bisecting a twelve-fixture gate by hand. This returns the same
-- {element_order, field, message} rows the extension's `duck_blocks_validate` does,
-- and check_conformance_macro.py compares the two so they cannot drift apart.
--
-- A boolean is the right thing for a gate and the wrong thing for a FAILING gate.
CREATE OR REPLACE MACRO duck_blocks_errors(blocks) AS TABLE (
    WITH e AS (SELECT unnest(blocks) AS el, generate_subscripts(blocks, 1) AS pos),
    lv AS (SELECT el, pos, el.level AS lvl, lag(el.level) OVER (ORDER BY pos) AS prev FROM e),
    dup AS (SELECT el.element_order AS ord FROM e GROUP BY 1 HAVING count(*) > 1)
    SELECT el.element_order AS element_order, 'kind' AS field,
           'Invalid kind ' || chr(39) || coalesce(el.kind, 'NULL') || chr(39)
             || '; see duck_block_kind_names()' AS message
      FROM e WHERE NOT list_contains(duck_block_declared_kinds(), el.kind)
    UNION ALL
    SELECT el.element_order, 'element_type', 'element_type is NULL'
      FROM e WHERE el.element_type IS NULL
    UNION ALL
    SELECT el.element_order, 'encoding',
           'Invalid encoding ' || chr(39) || el.encoding || chr(39)
      FROM e WHERE el.encoding IS NOT NULL
        AND NOT list_contains(['text','json','yaml','html','xml','latex','markdown'], el.encoding)
    UNION ALL
    SELECT el.element_order, 'level',
           CASE WHEN el.level IS NULL
                THEN 'level is NULL; every element carries an explicit depth, top level is 1'
                ELSE 'level ' || el.level || ' is below 1; top level is 1' END
      FROM e WHERE el.level IS NULL OR el.level < 1
    UNION ALL
    SELECT el.element_order, 'level',
           'level jumps from ' || prev || ' to ' || lvl
             || '; depth-first ordering descends one at a time, so this element''s parent is missing'
      FROM lv WHERE prev IS NOT NULL AND lvl > prev + 1
    UNION ALL
    SELECT el.element_order, 'element_order', 'element_order must be non-negative'
      FROM e WHERE el.element_order IS NULL OR el.element_order < 0
    UNION ALL
    SELECT ord, 'element_order', 'Duplicate element_order value' FROM dup
);

-- Document shape. The other 2, and they are the ones a per-element check CANNOT see:
--
--   duplicate element_order  needs the whole list
--   level jumping by >1      needs ADJACENCY. `level` is structural depth in a
--                            depth-first ordering, so a document descends one at a
--                            time; a jump means the element's parent is missing.
--                            This is the rule whose absence caused a year of drift
--                            across four extensions, and it is precisely the one a
--                            per-element macro cannot express -- so a consumer given
--                            only the element macro is unguarded against the defect
--                            most likely to bite them.
CREATE OR REPLACE MACRO duck_blocks_are_valid(blocks) AS (
    -- every element individually
    NOT EXISTS (SELECT 1 FROM unnest(blocks) AS t(e) WHERE NOT duck_block_is_valid(e))
    -- element_order unique
    AND (SELECT count(DISTINCT e.element_order) = count(*) FROM unnest(blocks) AS t(e))
    -- depth-first ordering descends one level at a time
    AND NOT EXISTS (
        SELECT 1 FROM (
            SELECT e.level AS lvl,
                   lag(e.level) OVER (ORDER BY i) AS prev
            FROM unnest(blocks) WITH ORDINALITY AS t(e, i)
        ) WHERE prev IS NOT NULL AND lvl > prev + 1
    )
);
