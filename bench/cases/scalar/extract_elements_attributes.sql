-- split: dev
-- function: xml_extract_elements
-- what: pull every <item> as an XML fragment and every item/discount attribute as structs from 200k order snippets
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_extract_elements(doc, '//item') AS items,
         xml_extract_attributes(doc, '//item | //discount') AS item_attrs
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
