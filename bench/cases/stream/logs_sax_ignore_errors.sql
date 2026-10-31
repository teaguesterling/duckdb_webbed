-- split: dev
-- function: read_xml
-- what: event-log export glob where one part is truncated mid-record; ignore_errors := true with maximum_file_size lowered to 1 MB so both parts stream via SAX and rows are withheld until each file parses cleanly; whole-file sample so inference types the intact part
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT * FROM read_xml('stream/dev/logs/*.xml', record_element := 'event',
                         ignore_errors := true, maximum_file_size := 1048576, filename := true,
                         sample_size := 100000)
) t;
