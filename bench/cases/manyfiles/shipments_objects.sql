-- split: holdout
-- function: read_xml_objects
-- what: 7k shipment manifests loaded as whole documents with filename, then per-file XPath summaries (carrier, shipment count, total weight, first recipient)
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           xml_extract_text(xml, '/manifest/@carrier')[1] AS carrier,
           len(xml_extract_text(xml, '//shipment/@id')) AS n_shipments,
           list_sum(list_transform(xml_extract_text(xml, '//shipment/@weight'), lambda x: x::DECIMAL(18,2))) AS total_weight,
           xml_extract_text(xml, '//shipment[1]/destination/recipient')[1] AS first_recipient
    FROM read_xml_objects('manyfiles/holdout/shipments/*.xml', filename := true)
) t;
