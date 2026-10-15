-- split: holdout
-- function: read_xml
-- what: two ~18 MB RSS 2.0 archive shards (content:encoded CDATA, dc:creator, slash:comments, nested media:content/media:credit) via glob, SAX inference and SAX scan per file, namespaces := 'strip', filename column, whole-file sample
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/holdout/rss/*.xml', record_element := 'item',
                         namespaces := 'strip', filename := true, sample_size := 100000)
) t;
