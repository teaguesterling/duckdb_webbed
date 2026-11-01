-- split: dev
-- function: read_xml
-- what: default inference over a 33 MB product catalog forced onto the DOM path: 8-level structs, lists of structs, rare optional children, attr+text leaves
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/catalog.xml', maximum_file_size := 200000000)
) t;
