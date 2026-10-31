# C: Join

A byte parser tracks quoted fields and doubled quote escapes. Only a BOM at a
record boundary is removed. Embedded newlines and trailing spaces remain data.
Regions map to compact country IDs through an integer array; unknown regions
go to the unmatched group. Money is parsed and formatted in signed integer cents.

Twelve workers parse record-aligned ranges. CRLF is a safe split point because the
generator contract excludes CR inside fields. Country aggregates are local per
worker. Exact name views share a concurrent open-addressed table, with atomic
country membership bits. Hash collisions compare bytes; a locked exact fallback
handles unusual table crowding. Publication uses acquire/release ordering, and
membership fetch-or counts each name/country pair once across all workers.

The CSV quote encoding is injective, so comparing encoded name payloads gives
exact decoded equality without allocating strings. Quoted and unquoted empty
payloads compare equal. Escapes are still counted to obtain decoded byte lengths.
Each distinct key and its country mask occupy a cache-line-aligned arena record.
The 16-byte table entries contain hash, offset and length; empty slots do not
allocate masks. Arena capacity includes input bytes and bounded slot overhead.
The table uses 32-bit arena offsets and lengths at the published sub-4-GB scale,
and an exact fallback for probe chains over 128 entries. Membership bits already
observed set need no atomic write. Country IDs fit in a byte (240 countries plus
the unmatched group). The short-window hash always checks full key bytes.
Notes are scanned without allocation. Input is mapped read-only, sums use 64-bit
integers, countries without records are omitted, and output keys sort bytewise.

No external dependencies. Optional arguments select input and output directories.
The offline compiler profile comes from full-size generated seed-17 data; only the
executable enters the final image. SD_THREADS supports 1-12 parsing workers.

With another 24 hours: reduce shared-table cache traffic and compare AVX2 quote
scanning against libc memchr. Correctness on quoted fields and concurrent name
deduplication remains a required gate.
