-- split: holdout
-- function: read_xml
-- what: attr_mode:='prefixed' on an event log with a different attribute mix (hex ids, decimals, optional attrs) and nested elements carrying attributes
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/holdout/events.xml', attr_mode := 'prefixed')
) t;
