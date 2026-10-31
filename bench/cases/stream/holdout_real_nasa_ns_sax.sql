-- split: holdout
-- function: read_xml
-- what: real NASA ADC dataset catalog (25 MB, xmlns:xlink on every record, deep nesting, UW XML repository, fetched by fetch.sh) streamed via SAX with default inference over every record (default namespace handling of xlink: attributes)
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/real/nasa.xml', record_element := 'dataset')
) t;
