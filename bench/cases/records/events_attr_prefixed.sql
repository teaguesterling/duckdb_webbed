-- split: dev
-- function: read_xml
-- what: attr_mode:='prefixed' on an attribute-heavy service event log (8-9 attributes per record, element with attributes plus text)
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/events.xml', attr_mode := 'prefixed')
) t;
