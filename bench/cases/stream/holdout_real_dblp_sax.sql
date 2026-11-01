-- split: holdout
-- function: read_xml
-- what: real DBLP bibliography (134 MB, ISO-8859-1, UW XML repository, fetched by fetch.sh) streamed via SAX, all inproceedings records with repeated author/ee lists, whole-file sample
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/real/dblp.xml', record_element := 'inproceedings', sample_size := 1000000)
) t;
