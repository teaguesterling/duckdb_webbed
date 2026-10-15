-- split: dev
-- function: read_xml
-- what: DOM catalog truncated with max_depth := 5, so subtrees below the cut are serialized to XML values instead of typed structs
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/catalog.xml', maximum_file_size := 200000000, max_depth := 5)
) t;
