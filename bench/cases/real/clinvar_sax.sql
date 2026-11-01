-- split: dev
-- function: read_xml
-- what: real ClinVar VCV release cut to 200 MB (fetch.py): deep attribute-heavy VariationArchive records streamed via SAX, polymorphic record shapes, LIST<STRUCT> fields
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/clinvar/vcv.xml', record_element := 'VariationArchive', force_list := ['Gene', 'SequenceLocation', 'HGVS', 'XRef', 'RCVAccession', 'ClinicalAssertion', 'Citation', 'Trait'])
) t;
