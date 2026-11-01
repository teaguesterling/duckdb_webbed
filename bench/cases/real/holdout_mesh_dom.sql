-- split: holdout
-- function: read_xml
-- what: real MeSH 2026 descriptors, the same 313 MB file through DOM: must match holdout_mesh_sax's fingerprint
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/mesh/desc2026.xml', record_element := 'DescriptorRecord', force_list := ['TreeNumber', 'Concept', 'Term', 'AllowableQualifier'], maximum_file_size := 400000000)
) t;
