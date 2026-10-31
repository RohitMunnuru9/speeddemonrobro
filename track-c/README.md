# C: Join

A byte parser tracks quoted fields and doubled quote escapes. Only a BOM at a
record boundary is removed. Embedded newlines and trailing spaces remain data.
Regions map to compact country IDs through an integer array; unknown regions
go to the unmatched group. Money is parsed and formatted in signed integer cents.

Country aggregates hold 64-bit sums. Every unescaped name is interned once,
with a 256-bit country membership mask that counts exact distinct names in each
country. This covers the specified 240 countries plus unmatched. Notes are scanned
for byte length without allocating their contents. Empty quoted and unquoted
names are the same value. Countries without records are omitted; output keys sort
bytewise. Input is mapped read-only to avoid copying the entire CSV into a buffer.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: compare independent parsing workers and vectorized quote
scanning, while accounting for merge costs. Correctness on the official CSV edge
cases is a required gate.
