-- split: dev
-- function: read_xml
-- what: 600 medium (~200 KB) vendor catalog files of ~430 products each read with one glob
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('manyfiles/dev/catalog/*.xml', record_element := 'product')
) t;
