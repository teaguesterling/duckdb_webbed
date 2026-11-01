-- split: dev
-- function: read_xml
-- what: real simplewiki stub-meta-history cut to 300 MB (fetch.py): revision records under page, default namespace, empty marker elements, contributor ip/username drift, streamed via SAX
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/wiki/stub_history.xml', record_element := 'revision')
) t;
