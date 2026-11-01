-- split: holdout
-- function: read_xml
-- what: real UniProtKB/Swiss-Prot cut to 300 MB (fetch.py): entry records redeclaring the default namespace, key-value dbReference/property attribute lists, long sequence text, streamed via SAX
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/uniprot/sprot.xml', record_element := 'entry', force_list := ['accession', 'dbReference', 'feature', 'comment', 'reference', 'keyword'])
) t;
