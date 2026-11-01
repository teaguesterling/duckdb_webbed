-- split: dev
-- function: read_xml
-- what: DOM catalog with force_list on 11 repeated element names, so single occurrences still build LIST(STRUCT) at every depth
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/catalog.xml',
        maximum_file_size := 200000000,
        force_list := ['section', 'group', 'attribute', 'variant', 'tier', 'warehouse', 'image', 'video', 'document', 'review', 'feature'])
) t;
