-- split: holdout
-- function: read_xml
-- what: SAX streaming of a 30 MB prefixed-namespace (o:) order export with ship_to, ext: loyalty and longer notes with an explicit columns schema including LIST<STRUCT> line items, unnested into per-category revenue
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT currency, li.category, year(placed_at) AS yr,
         count(*) AS lines, sum(li.qty) AS units, sum(li.qty * li.price) AS revenue,
         count(DISTINCT id) AS orders
  FROM (
    SELECT id, placed_at, currency, unnest(items.item) AS li
    FROM read_xml('stream/holdout/orders.xml', record_element := 'order',
         columns := {id: 'BIGINT', placed_at: 'TIMESTAMP', currency: 'VARCHAR',
                     items: 'STRUCT(item STRUCT(sku VARCHAR, qty INTEGER, price DECIMAL(10,2), category VARCHAR)[])'})
  )
  GROUP BY ALL
) t;
