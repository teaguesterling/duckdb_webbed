-- split: xl
-- function: read_xml
-- what: real UniProtKB/Swiss-Prot cut to 3 GB (fetch.py uniprot_xl): real/holdout_uniprot_sax at ten times the size
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/uniprot_xl/sprot.xml', record_element := 'entry', force_list := ['accession', 'dbReference', 'feature', 'comment', 'reference', 'keyword'])
) t;
