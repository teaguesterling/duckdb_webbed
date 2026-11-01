-- split: dev
-- function: xml_to_json
-- what: convert each order snippet to JSON with items and tags forced to arrays and empty elements as null
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_to_json(doc, force_list := ['item', 'tag'], empty_elements := 'null') AS js
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
