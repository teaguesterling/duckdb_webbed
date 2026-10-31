-- split: dev
-- function: xml_extract_text
-- what: 6000 small SOAP 1.1 envelopes (soap:, inv: prefixes, default ns redeclared in body) via read_xml_objects; XPath with an explicit namespace MAP and with namespaces := 'auto', plus xml_namespaces
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT filename,
    xml_extract_text(xml, '/soap:Envelope/soap:Header/inv:RequestId',
      namespaces := MAP {'soap': 'http://schemas.xmlsoap.org/soap/envelope/', 'inv': 'urn:acme:inventory:2019'}) AS request_id,
    xml_extract_text(xml, '//inv:Line/@sku',
      namespaces := MAP {'inv': 'urn:acme:inventory:2019'}) AS skus,
    xml_extract_text(xml, '//inv:Line/inv:OnHand', namespaces := 'auto') AS on_hand,
    xml_extract_text(xml, '//inv:Alert[@level="critical"]', namespaces := 'auto') AS critical,
    xml_namespaces(xml) AS ns
  FROM read_xml_objects('stream/dev/soap/*.xml', filename := true)
) t;
