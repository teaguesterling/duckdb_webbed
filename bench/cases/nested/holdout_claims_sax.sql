-- split: holdout
-- function: read_xml
-- what: holdout claims shape (extra plan/network/region level, 1-3 attachments per claim, longer notes) streamed on the SAX path with record_element := 'claim' and an explicit nested columns schema
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('nested/holdout/claims.xml',
        record_element := 'claim',
        columns := {
            'id': 'VARCHAR',
            'type': 'VARCHAR',
            'received': 'TIMESTAMP',
            'patient': 'STRUCT(member_id VARCHAR, "name" VARCHAR, dob DATE, coverage STRUCT(plan STRUCT(code VARCHAR, tier VARCHAR, network STRUCT("name" VARCHAR, region STRUCT(state VARCHAR))), active BOOLEAN))',
            'provider': 'STRUCT(npi VARCHAR, "name" VARCHAR, address STRUCT(city VARCHAR, country VARCHAR))',
            'lines': 'STRUCT(line STRUCT(n INTEGER, "procedure" STRUCT("#text" INTEGER, "system" VARCHAR), charge STRUCT("#text" DECIMAL(12,2), currency VARCHAR), units INTEGER, diagnoses STRUCT(dx VARCHAR[]), adjustments STRUCT(adjustment STRUCT("group" VARCHAR, reason INTEGER, amount DECIMAL(12,2))[]))[])',
            'attachments': 'STRUCT(attachment STRUCT("#text" VARCHAR, kind VARCHAR)[])',
            'appeal': 'STRUCT(filed DATE, "level" INTEGER, decision STRUCT(outcome VARCHAR))'
        })
) t;
