-- split: xl
-- function: read_xml
-- what: real OpenStreetMap planet dump cut to 3 GB of nodes (fetch.py osm): flat attribute-only records, most self-closing, tagged ones holding repeated k/v tag children
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/osm/planet.osm', record_element := 'node', force_list := ['tag'])
) t;
