-- split: dev
-- function: read_xml
-- what: bind-time inference alone (DESCRIBE) over the 240 drifting access-profile files, union_by_name:=true
SELECT count(*) AS n, sum(hash(column_name || column_type)::HUGEINT) AS fp FROM (
    DESCRIBE SELECT * FROM read_xml('bigunion/dev/profiles/*.xml', record_element := 'Profile', union_by_name := true, filename := true)
);
