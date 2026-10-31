-- split: dev
-- function: html_extract_table_rows
-- what: flatten every table cell (thead/tbody/tfoot, short rows) from a crawl table of 1500 page strings
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/dev/pages/*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename, unnest(html_extract_table_rows(content::HTML), recursive := true)
    FROM crawl
) t;
