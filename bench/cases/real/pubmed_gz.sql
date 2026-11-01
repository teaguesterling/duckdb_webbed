-- split: compression
-- function: read_xml
-- what: real PubMed 2026 baseline, two gzipped files from both ends of the baseline (fetch.py): decompressed and streamed via SAX. Must match holdout_pubmed_sax's fingerprint. Mixed-content abstracts, era drift across files
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/pubmed/*.xml.gz', record_element := 'PubmedArticle', union_by_name := true, force_list := ['Author', 'AbstractText', 'MeshHeading', 'Keyword', 'ArticleId', 'Reference'])
) t;
