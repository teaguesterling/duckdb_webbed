# webbed v2.7.0

A correctness release covering five reported issues. The headline is that **`html_extract_*`
now accepts plain `VARCHAR`** without an explicit `::HTML` cast, and that **a malformed XPath
raises instead of silently returning an empty result**. Shippable artifacts continue to build
against the **DuckDB v1.5.4** release tag.

Two of these change behavior in ways worth reading before upgrading: the XPath change (#134)
and the `html_extract_tables_json` return type (#130).

## `html_extract_*` accepts VARCHAR (#129)

Every `html_extract_*` function previously required an explicit `::HTML` cast, so the obvious
form failed:

```sql
SELECT html_extract_text(content, '//h2') FROM read_text('page.html');
-- No function matches ... candidates: html_extract_text(HTML, VARCHAR)
```

VARCHAR overloads are now registered alongside the `HTML` ones, so the cast is optional. The
existing `HTML`-typed calls are unchanged.

## Malformed XPath raises (#134) — behavior change

`xmlXPathEvalExpression` returns NULL for two unrelated reasons: the expression failed to
**parse**, and the expression parsed but could not be **evaluated**. All 33 call sites guarded
with `if (xpath_obj)` and fell through to an empty result, so a typo, an unclosed bracket, or a
CSS selector passed by mistake was indistinguishable from a valid expression that legitimately
matched nothing.

Compiling before evaluating separates the two cases:

| expression | result |
|---|---|
| `//h2` | `[Two]` — unchanged |
| `//h2[` | **now raises** `Invalid XPath expression: '//h2['` |
| `this is not xpath` | **now raises** |
| `h2` | `[]` — valid XPath, correctly matches nothing |
| `//gml:posList` (undeclared prefix) | `[]` — unchanged |

**Review before upgrading:** a query that silently returned empty because the expression was
malformed now raises `Invalid Input Error`. That is the point of the change — an empty result
meaning "your query is wrong" and one meaning "no matches" are different facts — but it will
surface expressions that were quietly broken.

An **undeclared namespace prefix keeps returning empty**. That case fails at evaluation, not at
parse, and the distinction is pinned by tests so it cannot regress.

Not addressed: `count(//h2)` still returns `[]` because the result is a number rather than a
node-set.

## `html_extract_tables_json` is usable at all (#130) — behavior change

Two faults, both reachable from a two-line document:

- A table with **no `<th>`** raised `INTERNAL Error: Value::LIST(values) cannot be used to make
  an empty list`. An INTERNAL error is an assertion failure and should not be reachable from
  input.
- The declared return type **never matched the value**. The `rows` field's element type was
  derived from the table's own header names (`STRUCT(h VARCHAR)` for a table with column `h`),
  but a scalar function's return type is fixed at bind time. Any cast, `len()`, or field access
  failed with `Mismatch Type Error` or `Could not find key "metadata" in struct`.

The content-dependent `rows` field is replaced by a fixed-shape **`table_json VARCHAR`**, so the
type no longer depends on the document. Callers destructure it with the JSON functions:

```sql
SELECT html_extract_tables_json(page)[1].table_json ->> '$.records[0].h';
```

**Review before upgrading:** this is a schema change to the returned struct. In practice nothing
could have depended on the old shape — every access to it raised.

## `<tfoot>` is modelled (#131)

A footer row was either silently dropped by `html_to_duck_blocks` or silently relabelled as data
by `html_extract_table_rows`, neither distinguishable from a table with no footer.
`html_extract_table_rows` now reports `row_type = 'footer'`.

## Valid JSON for control characters (#132)

`EscapeJsonString` escaped only `"`, `\`, `\n`, `\r` and `\t`; every other control character was
copied through verbatim, and JSON forbids unescaped bytes below `0x20`. A table or list block
built from HTML containing one — a vertical tab, say — carried content that was not valid JSON.
All C0 control characters are now `\uXXXX`-escaped.

## CI

The DuckDB-`main` canary shared a job name with the shippable build, so an expected upstream
breakage read as a failing required check on every pull request. It now runs only on `main` and
on manual dispatch, and is named as advisory. The format check, which had been commented out
entirely, is re-enabled.
