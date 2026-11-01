-- split: dev
-- function: parse_xml
-- what: concatenate 25k well-formed order snippets into one document and parse it with schema inference, one row per order
SET VARIABLE batch = (
  SELECT '<orders>' || string_agg(doc, '' ORDER BY id) || '</orders>'
  FROM read_parquet('scalar/dev/orders.parquet')
  WHERE id < 25600 AND xml_well_formed(doc)
);
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM parse_xml(getvariable('batch'), record_element := 'order')
) t;
