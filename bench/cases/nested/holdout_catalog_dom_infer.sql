-- split: holdout
-- function: read_xml
-- what: holdout shape (extra taxonomy/tolerance levels, dims as attributes, more variants, longer text) under default DOM inference
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/holdout/catalog.xml', maximum_file_size := 200000000)
) t;
