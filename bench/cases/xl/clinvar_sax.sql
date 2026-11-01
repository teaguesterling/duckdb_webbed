-- split: xl
-- function: read_xml
-- what: real ClinVar VCV release cut to 3 GB (fetch.py clinvar_xl): real/clinvar_sax at fifteen times the size, to check time and memory scale linearly
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/clinvar_xl/vcv.xml', record_element := 'VariationArchive', force_list := ['Gene', 'SequenceLocation', 'HGVS', 'XRef', 'RCVAccession', 'ClinicalAssertion', 'Citation', 'Trait'])
) t;
