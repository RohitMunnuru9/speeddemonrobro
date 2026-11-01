# Optimization log: C

Implementation began October 31, 2026, during the event window.

Version 1: parse CSV bytes directly, use a dense integer region lookup, and
aggregate exact integer cents and exact unescaped name sets. Local correctness
and timing evidence is recorded by tools/verify.py at repo root.

No board-measured version 1 exists; its results are local only.

Version 2: intern each exact unescaped name once with a country membership mask,
count note bytes without storing notes, and mmap the CSV read-only. On full seed
1 locally, RSS fell from 437,504 KiB to 213,732 KiB. Both versions matched the
published output hash. Wall time on the OneDrive mount varied substantially,
so no speed ratio is claimed from those runs.

First board baseline (version 2): `run-ca1317b2135c`, commit
`b7b22498b87bbe3012f23fc16f5f4fb0e09b04d2`, full public seed 1.
All gates passed. T = 0.972772 s, M = 14,352,384 bytes, S = 188, D = 1,
K = 6.782354, machine points = 41.569023. This is one practice-board run,
not the final five-run holdout result. The local RSS numbers above include
mapped input pages; the board's cgroup memory measurement is the scoring axis.

Next candidate: six CSV workers, allocation-free field views, and one shared exact
name table with atomic membership bits. Tested with full seeds 1 and 2 and a
60,000-record duplicate-name stress case spanning all workers. Local full-seed-1
runtime is about 0.32-0.40 s, versus roughly 2.35 s for the prior implementation
on native WSL storage. Board measurement pending; compare against run-ca1317b2135c.

Board confirmation for commit 3cb973f: run-ef82b1c61aac, all gates passed,
T=0.113590 s, M=19,742,720 bytes, S=310, K=17.275824, points=61.871939.
This is 8.56x faster than the first board baseline; K rose from 6.782354.
Next candidate aligns shared entries to cache lines and preserves hash low-bit
entropy. All existing correctness checks still pass; board measurement pending.

Board confirmation for cf4fee3: run-366e3b2fb77a, all gates passed,
T=0.098094 s, M=21,639,168 bytes, S=312, K=18.234840, points=63.045098.

Current candidate replaces cache-line padding with compact keys copied once into
an arena and uses LTO/profile guidance from a generated seed-17 sample. Local
Docker process medians were 0.155 s with 6 workers and 0.144 s with 12, with local
cgroup peaks about 20.3/20.6 MB. Docker has only four CPUs here, so these are not
predictions of the scoring machine. We selected 12 workers for a board trial.
Full seeds 1/2 and concurrent duplicate-name tests remain correctness gates.

PGO/arena receipt for dde1d70: run-b532ac44db17, all gates passed,
T=0.064260 s, M=46,096,384 bytes, S=328, K=19.222491, points=64.190483.
Time improved 1.53x against cf4fee3, but official memory rose substantially;
the earlier local cgroup figure did not predict the board peak.

Next candidate halves the table capacity to 131,072 slots, uses compact 32-bit
arena offsets/lengths, and stores country IDs in one byte. The existing exact
overflow map preserves correctness for long probe chains. A short-window hash
still compares all name bytes, and membership bits already observed set skip
fetch_or. Native full runs were roughly 0.20-0.28 s and 211-212 MB RSS including
mapped input. Full seeds 1/2 and concurrent edge checks pass. SIMD decimal
parsing added complexity without a clear gain and was rejected. Board pending.

Compact-table receipt for 9e77b66: run-160a4c4d2daf, all gates passed,
T=0.072499 s, M=36,007,936 bytes, S=346, K=18.861986, points=63.779370.
Memory fell, but time and K regressed against the previous best.

Next candidate separates 16-byte table metadata from occupied key/mask records,
aligns each record to 64 bytes and restores 262,144 slots. Payload capacity is
input bytes plus a bounded header/padding allowance per slot. Full seeds 1/2,
concurrent duplicate names and >128 deliberately identical short-window hashes
pass. Build-time training now uses full seed-17 data. Official measurement pending.

Arena receipt for 74f2b9b: run-4294a7441c5c, all gates passed,
T=0.092825 s, M=33,906,688 bytes, S=357, K=16.792088, points=61.255235.
This regressed; the best previous C result remains 64.190483 points.

Next candidate replaces the arena and metadata indirection with 64-byte inline
name/mask slots, a length-tagged 32-byte key and an exact long-key/crowding map.
Acquire/release publication protects key initialization; atomic membership
fetch-or remains exact across workers. A bounded AVX2 scan handles the first
32 bytes of a quoted field, then libc continues if needed. Scalar decimal
parsing remains: multiple packed-decimal variants failed to improve paired runs.

In three alternating native runs at twelve workers, the inline table before
the quote helper had median 0.2673 s versus 0.3617 s for our historical best-board
source (about 26% lower). A larger inline table was slower and was rejected.
These development timings are not board predictions. Both public full seeds,
the independent full seed 17, concurrent name duplicates, embedded LF/escaped
quotes/empty names and deliberately colliding sampled hashes pass. The offline
Docker full-public hash also passes. Official measurement is pending.

Inline-key receipt for a64a529: run-762f662d461c, all gates passed,
T=0.061856 s, M=13,987,840 bytes, S=354, K=24.586853, points=69.535147.
This improves on the previous best 64.190483 points, with substantially less
memory. It is a single public practice run, not the final holdout result.

The identical binary scored 69.772236 points in run-e29afdf54314 at 783111a,
T=0.060527 s, M=13,983,744 bytes, K=24.856772. Next trial uses six workers
to compare one worker per physical core against twelve hardware threads.
Linear probing remains: a quadratic-probing experiment did not provide a
reliable sufficient local gain. Full public hashes also pass with six workers.

Six-worker receipt for 3292dcb: run-d4353705ee04, all gates passed,
T=0.083441 s, M=12,251,136 bytes, S=354, K=21.737969, points=66.860948.
The smaller memory use did not repay lost throughput. Restore twelve workers,
whose K was 24.856772.

Next candidate constructs short padded name keys through a bounded AVX2 load
and byte-length mask. The last input bytes use the original copy path. Names
over 31 bytes still use the exact map; key length tags and country masks are
unchanged. Three alternating native runs gave median 0.09837 s against
0.11004 s for the previous source (about 10.6% lower). Both full public hashes
and tiny/sample seeds 1/2 pass. Official measurement is pending.

Receipt for 4539ae4: run-65df252c3dda, all gates passed,
T=0.058859 s, M=13,762,560 bytes, S=365, K=25.171220, points=70.045213.
This improves the prior best 69.772236 points. The full independent seed-17
reference and concurrent/CSV edge cases also match exactly.

Deferred layout trial splits each 64-byte key/mask slot into a 32-byte key and a
separate 32-byte mask. Total reserved bytes stay the same; membership atomics
no longer invalidate the key bytes in other workers' caches. Three alternating
native runs gave median 0.12268 s versus 0.12706 s (about 3.4% lower). This small
local difference requires an official comparison; exact key publication and
membership semantics are unchanged.
This experiment is not submitted; the best scored layout is retained while
work prioritizes B and D.

Priority changed to A/C. The selected C trial keeps exact 32-byte keys and the
existing table size, but overlaps parsing with random memory access. Each
worker retains 16 parsed records, prefetches region/home-slot addresses and
retires records after that look-ahead. Hashing is done once; key publication,
full equality and atomic membership remain unchanged. Queue wraparound and
partial tails are drained in input order. Explicit inlining keeps the parser
and retirement code in the same hot loop.

The prefetch look-ahead idea was informed by Cache Me's public C write-up:
https://github.com/manusingh090/speed-demon-2026-entry/blob/speed-demon/track-c/PERF.md
This implementation was independently written; it retains exact complete keys,
including the unbounded fallback, rather than probabilistic name identity.

One alternating local comparison gave medians 0.17769 s versus 0.25539 s for
the prior source (30.4% lower). Absolute local times are noisy and are not
official measurements. Eight/32/64-record queues, a CSV structural scanner,
and split short/long key tables did not improve on the selected 16-record
queue. Tiny/sample/full seeds 1 and 2 and all CSV/concurrent-membership edge
checks pass byte for byte. Official measurement is pending.

The actual offline PGO Docker build also passes the sealed full-seed-1 run and
the independent full seed-17 reference (61979ece5ecf40733491d28fd2022e6f04a85dd53c4629410dfeb48bc3d20591).

Receipt for 9f7e74a: run-d405cd96af9e, all gates passed,
T=0.052645 s, M=14,114,816 bytes, S=399, K=26.129731, points=70.856747.
The official speed gain is 6.2%, considerably smaller than the unprofiled local
comparison. Subsequent comparisons therefore use actual optimized PGO binaries.

Next trial separates 16-byte short keys and 32-byte long keys from their masks.
Reserved storage remains 8 MiB. Length tags and deterministic table routing
preserve exact identity; long strings and probe overflow still use exact maps.
Both the key and membership home addresses are prefetched in the existing
16-record queue. Key publication retains the acquire/release claim protocol.

A std-only Rust port was independently implemented and checked against the full
public hash. Its original layout was similar in speed to current C++. A compact
Rust version, including a PGO trial, was compared against compact C++. In the
last warm paired run, medians were 0.07914 s (current C++), 0.06997 s (compact
C++) and 0.07344 s (Rust PGO). Rust used about 0.7 MiB less local RSS, but did
not establish a faster overall candidate. These are local observations; the
compact C++ layout is selected for official comparison, not a language claim.

The compact layout passes tiny/sample/full seeds 1 and 2, concurrent/hostile CSV
edge cases, the sealed full-public Docker run and the actual PGO executable on
independent full seed 17. Official scoring remains pending.

Receipt for adb4712: run-a5bd15d5178c, all gates passed,
T=0.047419 s, M=13,881,344 bytes, S=443, K=27.193895, points=71.723571.
Against 9f7e74a, wall time fell 9.9% and K rose 4.1% despite the larger source.
The compact exact C++ layout is retained.
