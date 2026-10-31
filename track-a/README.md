# A: Ingest

This Rust entry maps the input read-only. Twelve workers first index contiguous
runs of each hour, preserving every run even when hours return out of order.
Each subsequent job exclusively owns one hour, so latency groups need no merge
or shared writes. Worker hash tables borrow endpoint bytes from the mapping;
sampled hashes always resolve collisions with full byte equality. Tables and
latency vector capacity are reused across hours.

Bounded AVX2 loads find spaces and LF within a 64-byte record window; longer
records and tails use scalar parsing. Known-length decimal conversion covers
the full uint32 latency range. Small groups are sorted; larger groups use exact
selection at the three nearest ranks. At most twelve hours of output are buffered,
then written in order to one file without a second joined copy.
Endpoints sort bytewise. Duplicate records count separately; all ranks and rates
use integer arithmetic.

No external crates. The allowlisted Rust compiler builds offline with thin LTO.
Unsafe code is limited to the read-only mapping, bounded byte searches, unaligned
in-slice loads and AVX2 parsing. Scoped threads cannot outlive their input.
Build and run the Dockerfile; optional arguments select local input/output paths.

With another 24 hours: compare persistent worker scheduling and radix selection,
including thread startup and memory costs. Retain changes when measured K improves.
