-- split: dev
-- function: html_extract_links
-- what: link and image audit (html_extract_links + html_extract_images) over read_html_objects of 1500 pages
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           html_extract_links(html) AS links,
           html_extract_images(html) AS images
    FROM read_html_objects('html/dev/pages/*.html', filename := true)
) t;
