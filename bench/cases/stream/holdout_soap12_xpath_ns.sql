-- split: holdout
-- function: xml_extract_text
-- what: 3000 SOAP 1.2 envelopes with WS-Addressing and WS-Security headers and larger pay: payment batches via read_xml_objects; XPath with an explicit namespace MAP and with namespaces := 'auto', plus xml_namespaces
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
  SELECT filename,
    xml_extract_text(xml, '/env:Envelope/env:Header/wsa:MessageID',
      namespaces := MAP {'env': 'http://www.w3.org/2003/05/soap-envelope', 'wsa': 'http://www.w3.org/2005/08/addressing'}) AS message_id,
    xml_extract_text(xml, '//pay:Amount/@ccy',
      namespaces := MAP {'pay': 'urn:initech:payments:v3'}) AS currencies,
    xml_extract_text(xml, '//pay:Payment/pay:Amount', namespaces := 'auto') AS amounts,
    xml_extract_text(xml, '//pay:Payment[pay:Instant="true"]/pay:Payer/pay:Name', namespaces := 'auto') AS instant_payers,
    xml_namespaces(xml) AS ns
  FROM read_xml_objects('stream/holdout/soap/*.xml', filename := true)
) t;
