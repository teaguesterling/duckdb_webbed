-- split: dev
-- function: xml_minify
-- what: reformat each snippet both ways (xml_minify and xml_pretty_print) over a mix of compact and indented input
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_minify(doc) AS compact,
         xml_pretty_print(doc) AS pretty
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
