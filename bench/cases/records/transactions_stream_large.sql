-- split: dev
-- function: read_xml
-- what: ~125 MB payment-transaction ledger (420k records) read with record_element:='txn' and an explicit columns:= schema, above maximum_file_size so it takes the SAX streaming path
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/dev/transactions.xml', record_element := 'txn', columns := {
        id: 'VARCHAR',
        type: 'VARCHAR',
        posted: 'TIMESTAMP',
        account: 'VARCHAR',
        amount: 'DECIMAL(12,2)',
        currency: 'VARCHAR',
        merchant: 'VARCHAR',
        mcc: 'INTEGER',
        reference: 'VARCHAR',
        cleared: 'BOOLEAN'
    })
) t;
