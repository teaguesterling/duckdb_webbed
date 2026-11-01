-- split: dev
-- function: read_xml
-- what: 4k device report files whose reading schema drifts v1..v4 (int->double, added/dropped fields, nested site), union_by_name:=true
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('manyfiles/dev/telemetry/*.xml', record_element := 'reading', union_by_name := true, filename := true)
) t;
