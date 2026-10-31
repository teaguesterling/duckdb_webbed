-- split: holdout
-- function: read_xml
-- what: real PubMed 2026 baseline, two files from both ends of the baseline (fetch.py) streamed via SAX, mixed-content abstracts, era drift across files
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/pubmed/*.xml', record_element := 'PubmedArticle', union_by_name := true, force_list := ['Author', 'AbstractText', 'MeshHeading', 'Keyword', 'ArticleId', 'Reference'])
) t;
