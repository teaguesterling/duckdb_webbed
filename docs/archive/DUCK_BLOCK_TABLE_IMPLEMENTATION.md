# Duck Block Table Implementation Guide

This document provides implementation guidance for rendering `duck_block` table elements to HTML, based on experience from the `duck_block_utils` Pandoc conversion work.

## How Tables Are Stored in duck_blocks

Tables are stored as a single block element with:
- `element_type`: `"table"`
- `encoding`: `"json"`
- `content`: The Pandoc table's `c` array as a JSON string

### Example duck_block for a table:

```sql
{
  kind: 'block',
  element_type: 'table',
  content: '[[],[{"t":"AlignDefault"},{"t":"AlignDefault"}],[0,0],[[{"t":"Plain","c":[{"t":"Str","c":"Col1"}]}],[{"t":"Plain","c":[{"t":"Str","c":"Col2"}]}]],[[[{"t":"Plain","c":[{"t":"Str","c":"A"}]}],[{"t":"Plain","c":[{"t":"Str","c":"B"}]}]]]]',
  level: NULL,
  encoding: 'json',
  attributes: {},
  element_order: 0
}
```

## Pandoc Table JSON Structure

The `content` field contains a JSON array with this structure:

```
[
  caption,           // [0] - Usually empty array []
  alignments,        // [1] - Array of {"t":"AlignDefault|AlignLeft|AlignCenter|AlignRight"}
  column_widths,     // [2] - Array of numbers (0 = auto width)
  headers,           // [3] - Array of header cells, each cell is [{"t":"Plain","c":[...inlines...]}]
  rows               // [4] - Array of rows, each row is array of cells
]
```

### Alignment Values
- `{"t":"AlignDefault"}` - No specific alignment
- `{"t":"AlignLeft"}` - Left align (`text-align: left`)
- `{"t":"AlignCenter"}` - Center align (`text-align: center`)
- `{"t":"AlignRight"}` - Right align (`text-align: right`)

### Cell Structure
Each cell contains Pandoc block elements (usually `Plain` or `Para`), which contain inline elements:

```json
{"t":"Plain","c":[{"t":"Str","c":"Cell text"}]}
{"t":"Plain","c":[{"t":"Strong","c":[{"t":"Str","c":"Bold"}]}]}
{"t":"Plain","c":[{"t":"Code","c":[["",[],""],"code text"]}]}
{"t":"Plain","c":[{"t":"Link","c":[["",[]],["Link text"],["http://example.com",""]]}]}
```

## Implementation Steps for HTML Conversion

### 1. Detect table blocks by checking encoding

```cpp
if (element_type == "table" && encoding == "json") {
    // Parse content as JSON and render as HTML table
}
```

### 2. Parse the JSON content

```cpp
// Parse content as JSON array
// content[0] = caption (can be used for <caption>)
// content[1] = alignments array
// content[2] = column widths (can be used for <col> width attributes)
// content[3] = header cells
// content[4] = body rows
```

### 3. Extract and convert inlines to HTML

Each cell contains Pandoc inlines. Recursively convert them:

```cpp
string RenderCellToHtml(const json& cell) {
    // cell is like [{"t":"Plain","c":[...inlines...]}]
    string result;
    for (auto& block : cell) {
        if (block["t"] == "Plain" || block["t"] == "Para") {
            result += RenderInlinesToHtml(block["c"]);
        }
    }
    return result;
}

string RenderInlinesToHtml(const json& inlines) {
    string result;
    for (auto& inline_elem : inlines) {
        string type = inline_elem["t"];
        if (type == "Str") {
            result += HtmlEscape(inline_elem["c"].get<string>());
        } else if (type == "Space") {
            result += " ";
        } else if (type == "Strong") {
            result += "<strong>" + RenderInlinesToHtml(inline_elem["c"]) + "</strong>";
        } else if (type == "Emph") {
            result += "<em>" + RenderInlinesToHtml(inline_elem["c"]) + "</em>";
        } else if (type == "Code") {
            result += "<code>" + HtmlEscape(inline_elem["c"][1].get<string>()) + "</code>";
        } else if (type == "Link") {
            string text = RenderInlinesToHtml(inline_elem["c"][1]);
            string url = HtmlEscape(inline_elem["c"][2][0].get<string>());
            result += "<a href=\"" + url + "\">" + text + "</a>";
        } else if (type == "Strikeout") {
            result += "<del>" + RenderInlinesToHtml(inline_elem["c"]) + "</del>";
        } else if (type == "Superscript") {
            result += "<sup>" + RenderInlinesToHtml(inline_elem["c"]) + "</sup>";
        } else if (type == "Subscript") {
            result += "<sub>" + RenderInlinesToHtml(inline_elem["c"]) + "</sub>";
        }
        // ... handle other inline types
    }
    return result;
}
```

### 4. Generate HTML table

```cpp
string GetAlignmentStyle(const json& align) {
    string type = align["t"].get<string>();
    if (type == "AlignLeft") return " style=\"text-align: left;\"";
    if (type == "AlignRight") return " style=\"text-align: right;\"";
    if (type == "AlignCenter") return " style=\"text-align: center;\"";
    return "";
}

string RenderTableToHtml(const json& table_content) {
    auto& alignments = table_content[1];
    auto& headers = table_content[3];
    auto& rows = table_content[4];

    ostringstream oss;
    oss << "<table>\n";

    // Optional: column width specifications
    // auto& widths = table_content[2];
    // if (!widths.empty()) {
    //     oss << "<colgroup>\n";
    //     for (auto& w : widths) {
    //         if (w.get<double>() > 0) {
    //             oss << "<col style=\"width: " << (w.get<double>() * 100) << "%;\">\n";
    //         } else {
    //             oss << "<col>\n";
    //         }
    //     }
    //     oss << "</colgroup>\n";
    // }

    // Header row
    if (!headers.empty()) {
        oss << "<thead>\n<tr>\n";
        for (size_t i = 0; i < headers.size(); i++) {
            string align_style = (i < alignments.size()) ? GetAlignmentStyle(alignments[i]) : "";
            oss << "<th" << align_style << ">" << RenderCellToHtml(headers[i]) << "</th>\n";
        }
        oss << "</tr>\n</thead>\n";
    }

    // Body rows
    if (!rows.empty()) {
        oss << "<tbody>\n";
        for (auto& row : rows) {
            oss << "<tr>\n";
            for (size_t i = 0; i < row.size(); i++) {
                string align_style = (i < alignments.size()) ? GetAlignmentStyle(alignments[i]) : "";
                oss << "<td" << align_style << ">" << RenderCellToHtml(row[i]) << "</td>\n";
            }
            oss << "</tr>\n";
        }
        oss << "</tbody>\n";
    }

    oss << "</table>";
    return oss.str();
}
```

## Expected Output

For a table with rich formatting:

```html
<table>
<thead>
<tr>
<th>Feature</th>
<th>Description</th>
<th>Status</th>
</tr>
</thead>
<tbody>
<tr>
<td><strong>Bold</strong></td>
<td>Text with <em>emphasis</em></td>
<td>Done</td>
</tr>
<tr>
<td><code>Code</code></td>
<td><a href="http://example.com">Link</a></td>
<td>Pending</td>
</tr>
</tbody>
</table>
```

## Testing

Test with this SQL after implementation:

```sql
-- Load both extensions
LOAD 'duck_block_utils';

-- Create a rich table via Pandoc and convert to HTML
SELECT duck_blocks_to_html(read_pandoc_ast('/path/to/table.json'));
```

Where `table.json` is created by:
```bash
echo '| A | B |
|---|---|
| **bold** | *italic* |' | pandoc -f markdown -t json > table.json
```

## Edge Cases to Handle

1. **Empty tables**: Tables with no rows should still output valid `<table></table>` or `<table><thead>...</thead></table>`

2. **Tables without headers**: Some tables may have empty header arrays - render only `<tbody>`

3. **Mismatched column counts**: Rows may have fewer cells than headers - handle gracefully

4. **Nested formatting**: Cells can contain nested inline formatting like `**bold with *nested italic***`

5. **Special characters**: Always HTML-escape text content to prevent XSS

## Related Issues

- Issue #62 in this repo tracks this implementation
