-- split: dev
-- function: read_xml
-- what: three ~17.5 MB Atom feed shards (default Atom ns plus media:, georss:, dc:, thr: prefixes) via glob, each over maximum_file_size so SAX inference and SAX scan per file, namespaces := 'strip', filename column, whole-file sample
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/dev/atom/*.xml', record_element := 'entry',
                         namespaces := 'strip', filename := true, sample_size := 100000)
) t;
