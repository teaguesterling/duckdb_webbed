-- split: dev
-- function: to_xml
-- what: serialize a typed order struct (varchar, nullable varchar, double, date, nullable boolean, list) to XML per row
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT id,
         to_xml({'id': id, 'customer': customer, 'country': country, 'total': total,
                 'placed': placed, 'gift': gift, 'tags': string_split(tags, '|')}, 'order') AS x
  FROM read_parquet('scalar/dev/orders.parquet')
) t;
