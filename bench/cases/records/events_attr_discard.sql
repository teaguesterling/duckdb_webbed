-- split: dev
-- function: read_xml
-- what: attr_mode:='discard' on the same attribute-heavy event log, so only element children become columns
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/events.xml', attr_mode := 'discard')
) t;
