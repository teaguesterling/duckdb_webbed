-- split: dev
-- function: read_html
-- what: scrape tbody rows from every product table across a directory of 1500 pages with an explicit td schema
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT *
    FROM read_html('html/dev/pages/*.html',
                   record_element := '//table/tbody/tr',
                   columns := {td: 'VARCHAR[]'},
                   filename := true)
) t;
