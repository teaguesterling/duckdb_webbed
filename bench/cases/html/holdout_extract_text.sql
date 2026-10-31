-- split: holdout
-- function: html_extract_text
-- what: full-page text plus h1/h2 XPath text over 700 wiki/forum pages (deep nesting, comment threads, long paragraphs)
CREATE TEMP TABLE crawl AS SELECT filename, content FROM read_text('html/holdout/pages/*.html');
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           html_extract_text(content::HTML) AS body_text,
           html_extract_text(content::HTML, '//h1 | //h2') AS headings
    FROM crawl
) t;
