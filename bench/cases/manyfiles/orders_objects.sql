-- split: dev
-- function: read_xml_objects
-- what: 10k small order files loaded as whole documents with filename, then per-file XPath summaries (batch, order count, gross total, first customer)
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT filename,
           xml_extract_text(xml, '/orders/@batch')[1] AS batch,
           len(xml_extract_text(xml, '//order/@id')) AS n_orders,
           list_sum(list_transform(xml_extract_text(xml, '//order/total'), lambda x: x::DECIMAL(18,2))) AS gross,
           xml_extract_text(xml, '//order[1]/customer')[1] AS first_customer
    FROM read_xml_objects('manyfiles/dev/orders/*.xml', filename := true)
) t;
