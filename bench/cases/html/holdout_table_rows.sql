-- split: holdout
-- function: html_extract_table_rows
-- what: flatten every table cell from 700 wiki/forum pages (many small wikitables, mostly no thead, row headers, sup citations)
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/holdout/pages/*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename, unnest(html_extract_table_rows(content::HTML), recursive := true)
    FROM crawl
) t;
