# C: Join

A byte parser tracks quoted fields and doubled quote escapes. Only a BOM at a
record boundary is removed. Embedded newlines and trailing spaces remain data.
Regions map to compact country IDs through an integer array; unknown regions
go to the unmatched group. Money is parsed and formatted in signed integer cents.

Six workers parse record-aligned ranges. CRLF is a safe split point because the
generator contract excludes CR inside fields. Country aggregates are local per
worker. Exact name views share a concurrent open-addressed table, with atomic
country membership bits. Hash collisions compare bytes; a locked exact fallback
handles unusual table crowding. Publication uses acquire/release ordering, and
membership fetch-or counts each name/country pair once across all workers.

The CSV quote encoding is injective, so comparing encoded name payloads gives
exact decoded equality without allocating strings. Quoted and unquoted empty
payloads compare equal. Escapes are still counted to obtain decoded byte lengths.
Notes are scanned without allocation. Input is mapped read-only, sums use 64-bit
integers, countries without records are omitted, and output keys sort bytewise.

No external dependencies. Optional arguments select input and output directories.

With another 24 hours: reduce shared-table cache traffic and compare AVX2 quote
scanning against libc memchr. Correctness on quoted fields and concurrent name
deduplication remains a required gate.
