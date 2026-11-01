-- split: dev
-- function: read_xml
-- what: real simplewiki stub-meta-history cut to 300 MB (fetch.py): page records each holding a list of up to thousands of revisions, streamed via SAX
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/wiki/stub_history.xml', record_element := 'page', force_list := ['revision'])
) t;
