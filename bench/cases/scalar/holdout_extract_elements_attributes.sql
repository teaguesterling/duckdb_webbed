-- split: holdout
-- function: xml_extract_elements
-- what: pull every <item> as an XML fragment and every item/discount attribute as structs from 70k larger order snippets (items nested under shipments)
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_extract_elements(doc, '//item') AS items,
         xml_extract_attributes(doc, '//item | //discount') AS item_attrs
  FROM read_parquet('scalar/holdout/orders.parquet')
) t;
