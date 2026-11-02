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
selection at the three nearest ranks. Persistent workers send completed hours
through rendezvous channels. Each worker can hold one pending output buffer,
plus the writer's current buffer, so output is bounded and written in order
without a second joined copy or a new thread for every hour.
Endpoints sort bytewise. Duplicate records count separately; all ranks and rates
use integer arithmetic.

The mapping owner outlives both scoped worker phases. Worker state drops its
borrowed endpoint keys before the scope joins; the owner then releases the
mapping once, including when writing the output returns an error. The parallel
teardown experiment was rejected after its official score was lower.

No external crates. The allowlisted Rust compiler builds offline with thin LTO.
Unsafe code is limited to the read-only mapping, bounded byte searches, unaligned
in-slice loads, AVX2 parsing and mapping release after all readers finish.
Build and run the Dockerfile; optional arguments select local input/output paths.

With another 24 hours: compare persistent worker scheduling and radix selection,
including thread startup and memory costs. Retain changes when measured K improves.
