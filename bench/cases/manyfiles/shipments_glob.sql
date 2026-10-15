-- split: holdout
-- function: read_xml
-- what: 7k small attribute-heavy shipment manifests with deeper nesting, one glob, record_element and filename:=true
-- note: the fingerprint reflects current output, which drops origin/destination @code and returns NULL for the attribute-only geo and customs elements (pre-existing data loss). Re-baseline if that is fixed.
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('manyfiles/holdout/shipments/*.xml', record_element := 'shipment', filename := true)
) t;
