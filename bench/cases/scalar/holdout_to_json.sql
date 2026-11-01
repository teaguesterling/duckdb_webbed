-- split: holdout
-- function: xml_to_json
-- what: holdout shape (nested shipments, item attributes, CDATA, namespaces): convert each order snippet to JSON with items and tags forced to arrays and empty elements as null
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_to_json(doc, force_list := ['item', 'tag'], empty_elements := 'null') AS js
  FROM read_parquet('scalar/holdout/orders.parquet')
) t;
