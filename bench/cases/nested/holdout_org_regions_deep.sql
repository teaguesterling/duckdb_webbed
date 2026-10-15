-- split: holdout
-- function: read_xml
-- what: holdout org shape (member address carries geo, 11 levels) with region records and unlimited max_depth
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/holdout/org.xml', record_element := 'region', max_depth := -1)
) t;
