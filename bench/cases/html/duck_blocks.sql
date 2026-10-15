-- split: dev
-- function: html_to_duck_blocks
-- what: convert 800 article pages to duck_block lists (headings, inline runs, lists, tables, figures)
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/dev/pages/page_00[0-7]*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename, html_to_duck_blocks(content::HTML) AS blocks
    FROM crawl
) t;
