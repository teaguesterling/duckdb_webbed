-- split: dev
-- function: read_xml
-- what: org chart with a mid-tree record_element (department), force_list on team/member/skill/project and sample_size := -1 (every value sampled): cross-record union of optional contact/skills/leave/projects
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/org.xml', record_element := 'department', force_list := ['team', 'member', 'skill', 'project'], sample_size := -1)
) t;
