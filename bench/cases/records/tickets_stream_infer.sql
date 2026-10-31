-- split: dev
-- function: read_xml
-- what: ~22 MB support-ticket export (9,000 large records with long bodies and threaded replies carrying attributes) above maximum_file_size, so record_element:='ticket' streams via SAX and infers the schema from the whole file
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/tickets.xml', record_element := 'ticket')
) t;
