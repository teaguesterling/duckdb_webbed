-- split: dev
-- function: read_xml
-- what: 10k small order batch files (1-20 KB) read with one glob, record_element and filename:=true
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('manyfiles/dev/orders/*.xml', record_element := 'order', filename := true)
) t;
