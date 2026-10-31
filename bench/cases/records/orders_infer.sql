-- split: dev
-- function: read_xml
-- what: default schema inference over a ~14 MB order export (DOM path) with attributes, nested customer/shipping structs, an item list, optional children and unicode text
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/orders.xml')
) t;
