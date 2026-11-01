-- split: dev
-- function: read_xml
-- what: org chart with region as the record: 10-level nesting (division/department/teams/team/members/member/contact/address) with unlimited max_depth
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/org.xml', record_element := 'region', max_depth := -1)
) t;
