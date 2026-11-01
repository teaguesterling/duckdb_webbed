-- split: dev
-- function: read_xml
-- what: 38 MB single-file order export (default xmlns on root) over maximum_file_size, so whole-file SAX schema inference plus SAX row streaming with nested LIST<STRUCT> items, all columns
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/dev/orders.xml', record_element := 'order', sample_size := 100000)
) t;
