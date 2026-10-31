-- split: dev
-- function: read_html_objects
-- what: 8k small article HTML pages loaded per file with filename, extracting title, date, links and images
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           html_extract_text(html, '//h1')[1] AS title,
           html_extract_text(html, '//time/@datetime')[1]::DATE AS published,
           html_extract_links(html) AS links,
           html_extract_images(html) AS images
    FROM read_html_objects('manyfiles/dev/pages/*.html', filename := true)
) t;
