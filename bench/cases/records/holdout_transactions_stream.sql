-- split: holdout
-- function: read_xml
-- what: ~68 MB transaction ledger with nested debit/credit structs, 4-decimal amounts and free-text memos, streamed via SAX with record_element:='txn' and an explicit columns:= schema
SELECT count(*) AS n, sum(hash(t)::HUGEINT) AS fp FROM (
    SELECT * FROM read_xml('records/holdout/transactions.xml', record_element := 'txn', columns := {
        txnId: 'BIGINT',
        valueDate: 'DATE',
        debit: 'STRUCT(iban VARCHAR, holder VARCHAR)',
        credit: 'STRUCT(iban VARCHAR, holder VARCHAR)',
        amount: 'DECIMAL(12,4)',
        fx: 'DOUBLE',
        memo: 'VARCHAR',
        instant: 'BOOLEAN'
    })
) t;
