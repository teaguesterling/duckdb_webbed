-- split: xl
-- function: read_xml
-- what: real simplewiki stub-meta-history cut to 3 GB (fetch.py wiki_xl): real/wiki_revisions at ten times the size
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/wiki_xl/stub_history.xml', record_element := 'revision')
) t;
