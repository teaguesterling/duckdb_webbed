-- split: dev
-- function: xml_extract_text
-- what: four XPath shapes per row (simple path, predicate, //descendant, positional attribute) over 200k order snippets
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         xml_extract_text(doc, '/order/customer/name') AS customer_name,
         xml_extract_text(doc, '//item[@qty > 2]/title') AS bulk_titles,
         xml_extract_text(doc, '//price') AS prices,
         xml_extract_text(doc, '(//item)[last()]/@sku') AS last_sku
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
