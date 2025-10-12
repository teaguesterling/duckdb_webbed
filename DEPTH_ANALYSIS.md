# Schema Inference at Different max_depth Levels

This document shows how the schema changes at different `max_depth` settings for three examples.

## Example 1: RSS Feed (test/xml/rss_feed.xml)

**Structure**:
```xml
<rss>                          <!-- Level 0: root -->
  <channel>                    <!-- Level 1: record -->
    <title>Tech News Feed</title>           <!-- Level 2 -->
    <link>https://example.com/feed</link>   <!-- Level 2 -->
    <description>Latest technology...</description>  <!-- Level 2 -->
    <language>en-us</language>              <!-- Level 2 -->
    <lastBuildDate>Mon, 01 Jan 2024...</lastBuildDate>  <!-- Level 2 -->
    <item>                                  <!-- Level 2 (appears 3x) -->
      <title>First Article</title>          <!-- Level 3 -->
      <link>https://example.com/...</link>  <!-- Level 3 -->
      <description>This is the first...</description>  <!-- Level 3 -->
      <pubDate>Mon, 01 Jan 2024 10:00...</pubDate>  <!-- Level 3 -->
      <guid>article-1</guid>                <!-- Level 3 -->
    </item>
    <item>...</item>  <!-- 2 more items -->
  </channel>
</rss>
```

### max_depth = 0
**Records**: 1 row (entire document)
**Schema**:
```
{
  rss: XML  // Column name matches root element name
}
```

### max_depth = 1
**Records**: 1 row (the `<channel>`)
**Schema**:
```
{
  channel: XML  // Column name matches record element name
}
```
**Note**: Record element returned as serialized XML

### max_depth = 2
**Records**: 1 row (the `<channel>`)
**Schema**:
```
{
  title: VARCHAR,
  link: VARCHAR,
  description: VARCHAR,
  language: VARCHAR,
  lastBuildDate: VARCHAR,
  item: XML  // Not introspected (has children, depth limit reached)
}
```
**Note**: `item` appears 3x but not introspected as LIST, returned as concatenated XML or XML array

### max_depth = 3
**Records**: 1 row (the `<channel>`)
**Schema**:
```
{
  title: VARCHAR,
  link: VARCHAR,
  description: VARCHAR,
  language: VARCHAR,
  lastBuildDate: VARCHAR,
  item: LIST<STRUCT<
    title: VARCHAR,
    link: VARCHAR,
    description: VARCHAR,
    pubDate: VARCHAR,
    guid: VARCHAR
  >>
}
```
**Note**: `item` is now recognized as LIST, its children are introspected as STRUCT

### max_depth = 4, 5, ... (no deeper structure)
Same as max_depth = 3

---

## Example 2: Simple Books (test/xml/simple.xml)

**Structure**:
```xml
<books>                        <!-- Level 0: root -->
  <book id="1" available="true">  <!-- Level 1: record (attributes!) -->
    <title>Database Systems</title>    <!-- Level 2 -->
    <author>John Smith</author>        <!-- Level 2 -->
    <price>49.99</price>               <!-- Level 2 -->
    <published>2024-01-15</published>  <!-- Level 2 -->
  </book>
  <book id="2" available="false">
    <title>XML Processing</title>
    <author>Jane Doe</author>
    <price>39.95</price>
    <published>2023-12-01</published>
  </book>
</books>
```

### max_depth = 0
**Records**: 1 row (entire document)
**Schema**:
```
{
  books: XML  // Column name matches root element name
}
```

### max_depth = 1
**Records**: 2 rows (the `<book>` elements)
**Schema**:
```
{
  book: XML  // Column name matches record element name
}
```
**Data**:
- Row 1: `{book: "<book id='1' available='true'>...</book>"}`
- Row 2: `{book: "<book id='2' available='false'>...</book>"}`

### max_depth = 2 (and higher - no deeper structure)
**Records**: 2 rows (the `<book>` elements)
**Schema** (with `attr_mode="columns"`):
```
{
  id: VARCHAR,        // From @id attribute
  available: VARCHAR, // From @available attribute
  title: VARCHAR,
  author: VARCHAR,
  price: DOUBLE,
  published: DATE
}
```
**Data**:
- Row 1: `{id: "1", available: "true", title: "Database Systems", author: "John Smith", price: 49.99, published: 2024-01-15}`
- Row 2: `{id: "2", available: "false", title: "XML Processing", author: "Jane Doe", price: 39.95, published: 2023-12-01}`

**Note**: All columns are scalars (leaf nodes), so max_depth=2,3,4,5 all produce same schema

---

## Example 3: Deep Hierarchy (test/xml/deep_hierarchy.xml)

**Structure** (simplified):
```xml
<organization>                     <!-- Level 0: root -->
  <dept:division id="DIV001" region="North America">  <!-- Level 1: record -->
    <name>Technology Division</name>                   <!-- Level 2 -->
    <dept:department id="DEPT001" budget="5000000">    <!-- Level 2 (appears 2x) -->
      <name>Software Engineering</name>                <!-- Level 3 -->
      <dept:team id="TEAM001" project="Alpha">         <!-- Level 3 (appears 2x) -->
        <name>Backend Development Team</name>          <!-- Level 4 -->
        <emp:manager id="MGR001">                      <!-- Level 4 -->
          <name>Sarah Chen</name>                      <!-- Level 5 -->
          <title>Senior Engineering Manager</title>    <!-- Level 5 -->
          <emp:details>                                <!-- Level 5 -->
            <hire_date>2019-03-15</hire_date>          <!-- Level 6 -->
            <salary currency="USD">145000</salary>     <!-- Level 6 -->
            <performance_rating>4.8</performance_rating>  <!-- Level 6 -->
            <emp:skills>                               <!-- Level 6 -->
              <skill level="expert">Python</skill>     <!-- Level 7 -->
              <skill level="advanced">Go</skill>       <!-- Level 7 -->
            </emp:skills>
          </emp:details>
        </emp:manager>
        <emp:employees>                                <!-- Level 4 -->
          <emp:employee id="EMP001" level="senior">    <!-- Level 5 -->
            <name>Alex Rodriguez</name>                <!-- Level 6 -->
            <emp:details>                              <!-- Level 6 -->
              <emp:projects>                           <!-- Level 7 -->
                <project id="PROJ001">                 <!-- Level 8 -->
                  <tasks>                              <!-- Level 9 -->
                    <task id="TASK001">                <!-- Level 10 -->
                      <subtasks>                       <!-- Level 11 -->
                        <subtask id="SUB001">          <!-- Level 12 -->
                          <!-- Goes even deeper... -->
                        </subtask>
                      </subtasks>
                    </task>
                  </tasks>
                </project>
              </emp:projects>
            </emp:details>
          </emp:employee>
        </emp:employees>
      </dept:team>
    </dept:department>
  </dept:division>
</organization>
```

### max_depth = 0
**Records**: 1 row (entire document)
**Schema**:
```
{
  organization: XML  // Column name matches root element name
}
```

### max_depth = 1
**Records**: 2 rows (the `<dept:division>` elements: DIV001, DIV002)
**Schema**:
```
{
  division: XML  // Column name matches record element name (namespace prefix stripped by default)
}
```
**Data**:
- Row 1: `{division: "<dept:division id='DIV001' region='North America'>...</dept:division>"}`
- Row 2: `{division: "<dept:division id='DIV002' region='Europe'>...</dept:division>"}`

### max_depth = 2
**Records**: 2 rows (the `<dept:division>` elements)
**Schema** (with `attr_mode="columns"`):
```
{
  id: VARCHAR,              // From @id attribute on division
  region: VARCHAR,          // From @region attribute on division
  name: VARCHAR,            // Leaf element (scalar)
  department: XML           // Has children, not introspected (depth limit)
}
```
**Data**:
- Row 1: `{id: "DIV001", region: "North America", name: "Technology Division", department: "<dept:department id='DEPT001'>...</dept:department><dept:department id='DEPT002'>...</dept:department>"}`
- Row 2: `{id: "DIV002", region: "Europe", name: "Operations Division", department: "<dept:department id='DEPT003'>...</dept:department>"}`

### max_depth = 3
**Records**: 2 rows
**Schema** (with `attr_mode="columns"`):
```
{
  id: VARCHAR,
  region: VARCHAR,
  name: VARCHAR,
  department: LIST<STRUCT<      // Repeated element, now introspected as LIST
    id: VARCHAR,                // From @id attribute
    budget: VARCHAR,            // From @budget attribute
    name: VARCHAR,              // Scalar child
    team: XML                   // Has children, depth limit reached
  >>
}
```

### max_depth = 4
**Records**: 2 rows
**Schema**:
```
{
  id: VARCHAR,
  region: VARCHAR,
  name: VARCHAR,
  department: LIST<STRUCT<
    id: VARCHAR,
    budget: VARCHAR,
    name: VARCHAR,
    team: LIST<STRUCT<          // Now introspected
      id: VARCHAR,              // From @id attribute
      project: VARCHAR,         // From @project attribute
      name: VARCHAR,            // Scalar child
      manager: XML,             // Has children, depth limit reached
      employees: XML            // Has children, depth limit reached
    >>
  >>
}
```

### max_depth = 5
**Records**: 2 rows
**Schema**:
```
{
  id: VARCHAR,
  region: VARCHAR,
  name: VARCHAR,
  department: LIST<STRUCT<
    id: VARCHAR,
    budget: VARCHAR,
    name: VARCHAR,
    team: LIST<STRUCT<
      id: VARCHAR,
      project: VARCHAR,
      name: VARCHAR,
      manager: STRUCT<           // Single element, introspected as STRUCT
        id: VARCHAR,             // From @id
        name: VARCHAR,           // Scalar
        title: VARCHAR,          // Scalar
        details: XML             // Has children, depth limit reached
      >,
      employees: STRUCT<         // Single element wrapping employee list
        employee: LIST<STRUCT<   // Multiple employees
          id: VARCHAR,           // From @id
          level: VARCHAR,        // From @level
          name: VARCHAR,         // Scalar
          title: VARCHAR,        // Scalar
          details: XML           // Has children, depth limit reached
        >>
      >
    >>
  >>
}
```
**Note**: At depth 5, we can see manager and employees structure, but their `<emp:details>` children are XML (depth limit)

---

## Summary Table

| max_depth | RSS Feed Columns | Books Columns | Deep Hierarchy Columns |
|-----------|-----------------|---------------|------------------------|
| 0 | `rss: XML` | `books: XML` | `organization: XML` |
| 1 | `channel: XML` | `book: XML` | `division: XML` |
| 2 | `title, link, description, language, lastBuildDate, item: XML` | `id, available, title, author, price, published` | `id, region, name, department: XML` |
| 3 | `title, link, ..., item: LIST<STRUCT<...>>` | Same as depth 2 | `id, region, name, department: LIST<STRUCT<...>>` |
| 4 | Same as depth 3 | Same as depth 2 | Deeper introspection of team |
| 5 | Same as depth 3 | Same as depth 2 | Deeper introspection of manager/employees |

## Column Naming Logic

**max_depth = 0**:
- Column name = root element name
- RSS: `rss`, Books: `books`, Deep: `organization`

**max_depth = 1**:
- Column name = record element name
- RSS: `channel`, Books: `book`, Deep: `division`

**max_depth >= 2**:
- Column names = children of record elements
- RSS: `title`, `link`, `item`, etc.
- Books: `id`, `title`, `author`, etc.
- Deep: `id`, `name`, `department`, etc.

**Namespace Handling**:
- By default, namespace prefixes are stripped from column names
- `<dept:division>` → column name `division`
- Controlled by `namespaces` parameter: `"strip"` (default) | `"keep"` | `"expand"`

## Key Observations

1. **max_depth=0**: Single column named after root element, entire document as XML
2. **max_depth=1**: Single column named after record element, each record as XML (useful for chunking)
3. **max_depth=2**: Flat columns named after record's children, complex children become XML (good for simple tabular data)
4. **max_depth=3+**: Enables LIST and STRUCT types for nested data
5. **Flat documents** (like Books): Benefit from depth=2, higher depths don't change schema
6. **Nested documents** (like RSS, Deep Hierarchy): Need depth=3+ for structured nested types
7. **Very deep documents** (Deep Hierarchy): Each increment reveals one more level of structure
8. **Column names are semantic**: Always match the actual XML element names, not generic "xml"

## SQL Usage Examples

```sql
-- max_depth = 0: Get entire document as single XML column
SELECT rss FROM read_xml('feed.xml', max_depth := 0);

-- max_depth = 1: Get records as XML chunks
SELECT book FROM read_xml('catalog.xml', max_depth := 1);

-- max_depth = 2: Get flat columns only
SELECT title, author, price FROM read_xml('catalog.xml', max_depth := 2);

-- max_depth = 3: Get nested structures (RSS with items as LIST<STRUCT>)
SELECT title, item FROM read_xml('feed.xml', max_depth := 3);

-- max_depth = -1: Full introspection (default)
SELECT * FROM read_xml('deep_hierarchy.xml', max_depth := -1);
```

## Default Value Considerations

### Option 1: `max_depth = -1` (unlimited)
**Pros**:
- Full introspection, users get maximally structured data
- Works well for RSS, nested JSON-like XML, etc.

**Cons**:
- Could be slow on very deep documents
- Might infer overly complex schemas
- Deep Hierarchy example goes 12+ levels deep

### Option 2: `max_depth = 2`
**Pros**:
- Fast, simple flat columns
- Good for traditional tabular XML (like Books)

**Cons**:
- Doesn't handle nested data well
- RSS would have `item: XML` instead of `item: LIST<STRUCT>`

### Option 3: `max_depth = 4` or `5` (reasonable fixed limit)
**Pros**:
- Handles most common cases (RSS needs 3, most nested docs need 3-5)
- Prevents excessive introspection on pathological documents
- Predictable performance

**Cons**:
- Arbitrary cutoff
- Might still be too deep or too shallow for some docs

### Option 4: Dynamic based on schema consistency (adaptive)
**Pros**:
- Intelligent - stops when structure becomes heterogeneous/inconsistent
- Different documents get appropriate depth automatically
- RSS would naturally stop at depth 3 (items are homogeneous)
- Deep Hierarchy might stop earlier if employee details vary wildly

**Algorithm**:
```
Start at depth 2 (always try flat columns)
For each depth level:
  - Check if this level has homogeneous, consistent structure
  - If yes: continue to next depth
  - If no (heterogeneous): stop here, use XML type for this level
  - Stop at max of 10 levels (safety limit)
```

**Cons**:
- More complex to implement
- Less predictable for users
- Different files might get different schemas even if structure is similar

**Heterogeneity indicators**:
- Mixed content (some elements have text, others have children)
- Varying child structure across instances
- High attribute variation
- Optional fields appearing < 50% of the time

### Recommendation

I lean toward **Option 3: `max_depth = 5`** as the default:

1. Handles RSS (depth 3 needed) ✓
2. Handles most nested documents (3-5 levels typical) ✓
3. Prevents excessive introspection on pathological documents ✓
4. Predictable and easy to understand ✓
5. Users can override with `max_depth := -1` for deep documents

Or **Option 1: `max_depth = -1`** with a **safety limit of 10** to prevent runaway:
```cpp
// In code: max_depth == -1 means unlimited, but cap at 10 for safety
int effective_depth = (options.max_depth == -1) ? 10 : options.max_depth;
```

This way:
- Default behavior is "full introspection" (user-friendly)
- But we don't blow up on 50-level deep XML
- Documents deeper than 10 levels get XML types at depth 11+

**What do you prefer?**
