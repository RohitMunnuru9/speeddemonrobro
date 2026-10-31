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
