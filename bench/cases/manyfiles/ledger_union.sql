-- split: holdout
-- function: read_xml
-- what: 4k ledger files across 5 interleaved schema revisions (int->decimal amounts, optional nested dims), union_by_name:=true
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('manyfiles/holdout/ledger/*.xml', record_element := 'entry', union_by_name := true, filename := true)
) t;
