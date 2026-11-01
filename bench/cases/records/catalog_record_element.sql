-- split: dev
-- function: read_xml
-- what: record_element:='product' picks products nested under vendor/category wrappers in a ~14 MB catalog with long descriptions, a dimensions struct and a tag list
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/catalog.xml', record_element := 'product')
) t;
