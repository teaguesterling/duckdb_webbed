-- split: holdout
-- function: html_to_duck_blocks
-- what: convert 700 wiki/forum pages to duck_block lists (deep div nesting, nested comment lists, wikitables)
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/holdout/pages/*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename, html_to_duck_blocks(content::HTML) AS blocks
    FROM crawl
) t;
