-- split: holdout
-- function: read_xml
-- what: real MeSH 2026 descriptors, 313 MB single file (fetch.py): DescriptorRecord with repeated AllowableQualifier and Concept/Term lists, streamed via SAX
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/mesh/desc2026.xml', record_element := 'DescriptorRecord', force_list := ['TreeNumber', 'Concept', 'Term', 'AllowableQualifier'])
) t;
