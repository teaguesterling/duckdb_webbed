-- split: dev
-- function: read_xml
-- what: explicit columns:= (no inference) on weather-station readings where 18 of 19 fields are integers, doubles, decimals, dates, timestamps or booleans, so value conversion dominates
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/readings.xml', columns := {
        seq: 'BIGINT',
        station: 'INTEGER',
        observed_at: 'TIMESTAMP',
        obs_date: 'DATE',
        temp_c: 'DECIMAL(6,2)',
        dewpoint_c: 'DECIMAL(6,2)',
        humidity_pct: 'DOUBLE',
        pressure_hpa: 'DECIMAL(7,1)',
        wind_ms: 'DOUBLE',
        wind_dir: 'SMALLINT',
        gust_ms: 'DOUBLE',
        rain_mm: 'DOUBLE',
        solar_wm2: 'INTEGER',
        battery_v: 'DECIMAL(5,3)',
        samples: 'INTEGER',
        counter: 'BIGINT',
        calibrated_on: 'DATE',
        heater_on: 'BOOLEAN',
        qc_pass: 'BOOLEAN'
    })
) t;
