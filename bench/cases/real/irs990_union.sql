-- split: dev
-- function: read_xml
-- what: real IRS Form 990 e-file returns, 5,000 files (fetch.py): default namespace, form types 990/EZ/PF/T with different ReturnData subtrees, union_by_name over extreme drift
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('real/irs990/*.xml', record_element := 'Return', union_by_name := true, filename := true)
) t;
