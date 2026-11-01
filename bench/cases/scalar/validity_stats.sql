-- split: dev
-- function: xml_valid
-- what: well-formedness checks plus structural stats (xml_valid, xml_well_formed, xml_stats) on 200k snippets with ~2% malformed
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_valid(doc) AS valid,
         xml_well_formed(doc) AS well_formed,
         xml_stats(doc) AS stats
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
