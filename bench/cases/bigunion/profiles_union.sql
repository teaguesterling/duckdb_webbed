-- split: dev
-- function: read_xml
-- what: 240 access-profile files of 2-8 MB (1.2 GB, all DOM) whose optional sections and children drift, one record per file, union_by_name:=true, filename:=true
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('bigunion/dev/profiles/*.xml', record_element := 'Profile', union_by_name := true, filename := true)
) t;
