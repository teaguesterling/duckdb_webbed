-- split: dev
-- function: html_extract_text
-- what: full-page text plus h1/h2 XPath text over a crawl table of 1500 page strings
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/dev/pages/*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           html_extract_text(content::HTML) AS body_text,
           html_extract_text(content::HTML, '//h1 | //h2') AS headings
    FROM crawl
) t;
