# Documentation Review Report

**Date:** January 2, 2026
**Current Version:** v1.3.1
**Actual Test Stats:** 58 test suites, 1901 assertions

---

## Executive Summary

The documentation is generally comprehensive but has several consistency issues, outdated statistics, and a few missing functions. The biggest issues are:
1. Outdated test statistics in 3 locations
2. Two undocumented functions from v1.3.0
3. Incorrect return type descriptions for xpath functions
4. Changelog missing v1.3.1

---

## Issues by Priority

### HIGH PRIORITY

#### 1. Undocumented Functions
The following functions exist but are NOT documented anywhere:

| Function | What it does | Where to add |
|----------|--------------|--------------|
| `xml_lookup_namespace(prefix)` | Returns URI for common namespace prefix (e.g., 'gml' → 'http://www.opengis.net/gml') | utilities.rst |
| `xml_find_undefined_prefixes(xml, xpath)` | Returns list of namespace prefixes used in xpath but not declared in xml | utilities.rst |

**Evidence:**
```sql
SELECT xml_lookup_namespace('gml');  -- Returns: http://www.opengis.net/gml
SELECT xml_find_undefined_prefixes('<root><gml:pos/></root>', '//gml:pos');  -- Returns: [gml]
```

#### 2. Incorrect Return Type Documentation
**Location:** `docs/functions/index.rst` lines 56-61, `docs/functions/xml_extraction.rst`

**Problem:** Documentation says `xml_extract_text` returns "first text match" (VARCHAR), but it actually returns `VARCHAR[]` (a list of ALL matches).

**Actual behavior:**
```sql
SELECT typeof(xml_extract_text('<r><i>a</i><i>b</i></r>', '//i'));  -- VARCHAR[]
SELECT xml_extract_text('<r><i>a</i><i>b</i></r>', '//i');  -- [a, b]
```

Same issue affects:
- `xml_extract_elements` - docs say "first matching element" but returns `xmlfragment[]`

#### 3. Outdated Test Statistics
| Location | Current Value | Actual Value |
|----------|---------------|--------------|
| README.md:870 | 1608 assertions | 1901 assertions |
| docs/changelog.rst:23 | 56 suites, 1691 assertions | 58 suites, 1901 assertions |
| docs/index.rst:59 | 56 suites, 1691 assertions | 58 suites, 1901 assertions |

Also README says "55 comprehensive test suites" (line 868) which should be 58.

#### 4. Changelog Missing v1.3.1
The changelog only goes up to v1.3.0. Need to add v1.3.1 entry for the NULL content rendering fix.

---

### MEDIUM PRIORITY

#### 5. Quick Reference Table Missing Functions
**Location:** `docs/functions/index.rst` - Utility Functions section

Missing from quick reference:
- `xml_common_namespaces()` - Returns map of well-known namespace prefixes
- `xml_detect_prefixes(xpath)` - Detect prefixes in XPath expression
- `xml_mock_namespaces(prefixes)` - Create mock URIs for prefixes
- `xml_add_namespace_declarations(xml, map)` - Inject namespace declarations
- `xml_lookup_namespace(prefix)` - Look up common namespace URI (UNDOCUMENTED)
- `xml_find_undefined_prefixes(xml, xpath)` - Find undefined prefixes (UNDOCUMENTED)

These ARE documented in utilities.rst but not in the quick reference.

#### 6. Namespace Mode Inconsistency
**Problem:** Different documentation pages describe namespace modes differently:

| Location | Modes Listed |
|----------|--------------|
| parameters.rst | 'strip', 'expand', 'keep' (for read_xml) |
| namespaces.rst | 'auto', 'strict', 'ignore' (for XPath functions) + 'strip', 'expand', 'keep' |
| xml_extraction.rst | 'auto', 'strict', 'ignore' (for XPath functions) |
| xpath_guide.rst | Only mentions `local-name()` workaround, not 'auto' mode |

**Recommendation:** Make xpath_guide.rst mention the recommended `namespaces := 'auto'` approach prominently.

#### 7. Confusing max_depth Description
**Location:** parameters.rst:62, schema_inference.rst:158

**Problem:** Says "Unlimited (capped at 10 for safety)" which is contradictory.

**Current text:** "Maximum parsing depth (-1 for unlimited, capped at 10 for safety)"

**Suggested fix:** Clarify that -1 means "use default" which IS 10, not truly unlimited.

#### 8. Sparse Documentation for Some Functions
These functions have minimal documentation with no examples:
- `html_extract_table_rows` - Just says "Returns: LIST<STRUCT>"
- `html_extract_tables_json` - No example of the JSON structure returned
- `read_html` differences - Just says "Same parameters as read_xml" without HTML-specific details

---

### LOW PRIORITY

#### 9. README vs Docs Redundancy
The README.md (895 lines) duplicates significant content from the docs:
- Full function reference tables
- Extensive examples
- Parameter documentation

**Recommendation:** Consider trimming README to essentials and linking to readthedocs for details.

#### 10. Build Instructions Mismatch
**Location:** docs/installation.rst vs CLAUDE.md

- installation.rst shows: `make release`
- CLAUDE.md shows: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`

**Recommendation:** Clarify that the simple command works when vcpkg is already set up.

#### 11. UPDATING.md Path Typo
**Location:** docs/UPDATING.md

Contains `./github/workflows` but should be `./.github/workflows`

#### 12. Quickstart Link Extraction Example Inefficiency
**Location:** docs/quickstart.rst lines 99-107

Shows redundant pattern calling function twice:
```sql
SELECT
    (unnest(html_extract_links(html))).href,
    (unnest(html_extract_links(html))).text  -- Called twice!
```

**Better pattern:**
```sql
SELECT links.href, links.text
FROM read_html_objects('*.html'),
     LATERAL unnest(html_extract_links(html)) AS links;
```

#### 13. Archive Files
**Location:** docs/archive/

Contains obsolete planning documents:
- XML_EXTENSION_PROJECT_PLAN.md
- XML_EXTENSION_DESIGN.md
- TEST_STATUS.md

**Recommendation:** Either remove from docs/ or add note that these are historical.

#### 14. External Extension Dependency Examples
**Location:** docs/functions/conversion.rst

The duck_block_utils integration examples assume that extension is installed. Should note it's optional/separate.

---

## Cross-Reference Issues

| From | Should Link To |
|------|----------------|
| xpath_guide.rst | namespaces.rst (for 'auto' mode) |
| parameters.rst | namespaces.rst (for namespace modes explanation) |
| changelog.rst | GitHub issues (currently just #numbers, not links) |

---

## Recommended Actions

### Immediate (Before v1.3.1 release notes)
1. Add v1.3.1 to changelog
2. Document `xml_lookup_namespace` and `xml_find_undefined_prefixes`
3. Fix return type descriptions for xpath functions

### Short-term
4. Update all test statistics to 58/1901
5. Add missing functions to quick reference table
6. Fix max_depth description clarity

### Medium-term
7. Improve sparse function documentation
8. Add cross-references between related pages
9. Consider README trimming

---

## Files Requiring Updates

| File | Changes Needed |
|------|----------------|
| docs/changelog.rst | Add v1.3.1, fix test stats |
| docs/index.rst | Fix test stats |
| docs/functions/index.rst | Fix return type descriptions, add missing functions |
| docs/functions/utilities.rst | Add xml_lookup_namespace, xml_find_undefined_prefixes |
| docs/functions/xml_extraction.rst | Fix return type (VARCHAR → VARCHAR[]) |
| docs/xpath_guide.rst | Add prominent mention of 'auto' namespace mode |
| docs/parameters.rst | Clarify max_depth description |
| README.md | Fix test stats (lines 868-870) |
| docs/UPDATING.md | Fix path typo |
