-- split: holdout
-- function: read_xml
-- what: default schema inference over a ~14 MB purchase-order export with a different shape: geo struct under buyer address, 1-12 line items without attributes, timestamps, CDATA comments
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/holdout/orders.xml')
) t;
