-- split: dev
-- function: html_extract_tables
-- what: one 21 MB data-export page with a 37k-row table, extracted with the html_extract_tables table function
SET VARIABLE report = (SELECT content FROM read_text('html/dev/large/report.html'));
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM html_extract_tables(getvariable('report'))
) t;
