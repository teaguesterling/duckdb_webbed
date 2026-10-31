-- split: dev
-- function: html_extract_text
-- what: real Common Crawl CC-MAIN-2024-51, first 2,000 HTML 200 responses of one WARC (fetch.py), the 1,803 that decode as UTF-8: malformed markup, text, links, and tables over a page table
CREATE TEMP TABLE crawl AS SELECT filename, content FROM (
    SELECT filename, TRY(decode(content)) AS content FROM read_blob('real/cc/pages/*.html')
) WHERE content IS NOT NULL;
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           length(html_extract_text(content::HTML)) AS text_len,
           len(html_extract_links(content::HTML)) AS links,
           len(html_extract_tables_json(content::HTML)) AS tables
    FROM crawl
) t;
