-- split: dev
-- function: read_xml
-- what: 30 MB healthcare claims export streamed on the SAX path (record_element := 'claim' under batch wrappers) with an explicit nested columns schema: patient/coverage/plan structs, provider attr+children, lists of lines with dx and adjustment lists, attachment list
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/dev/claims.xml',
        record_element := 'claim',
        columns := {
            'id': 'VARCHAR',
            'type': 'VARCHAR',
            'received': 'TIMESTAMP',
            'patient': 'STRUCT(member_id VARCHAR, "name" VARCHAR, dob DATE, coverage STRUCT(plan STRUCT(code VARCHAR, tier VARCHAR), active BOOLEAN))',
            'provider': 'STRUCT(npi VARCHAR, "name" VARCHAR, address STRUCT(city VARCHAR, country VARCHAR))',
            'lines': 'STRUCT(line STRUCT(n INTEGER, "procedure" STRUCT("#text" INTEGER, "system" VARCHAR), charge STRUCT("#text" DECIMAL(12,2), currency VARCHAR), units INTEGER, diagnoses STRUCT(dx VARCHAR[]), adjustments STRUCT(adjustment STRUCT("group" VARCHAR, reason INTEGER, amount DECIMAL(12,2))[]))[])',
            'attachments': 'STRUCT(attachment STRUCT("#text" VARCHAR, kind VARCHAR)[])',
            'appeal': 'STRUCT(filed DATE, "level" INTEGER, decision STRUCT(outcome VARCHAR))'
        })
) t;
