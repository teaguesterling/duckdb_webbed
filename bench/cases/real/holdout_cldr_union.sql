-- split: holdout
-- function: read_xml
-- what: real Unicode CLDR 46 common/main, 1,067 locale files (fetch.py): attribute-keyed deep nesting, extreme drift between base and regional locales, union_by_name over DOM
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/cldr/*.xml', record_element := 'ldml', union_by_name := true, filename := true)
) t;
