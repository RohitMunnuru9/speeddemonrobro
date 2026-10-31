# Optimization log: A

Implementation began October 31, 2026, during the event window.

Version 1: replace per-record datetime parsing with fixed-span hour buckets and
store latencies as uint32 values. Keep full sorting as a simple exact baseline.
Local correctness and timing evidence is recorded by tools/verify.py at repo root.

First board baseline: `run-9b28b732aad3`, commit
`b7b22498b87bbe3012f23fc16f5f4fb0e09b04d2`, full public seed 1.
All gates passed. T = 0.702697 s, M = 34,725,888 bytes, S = 80, D = 1,
K = 9.326661, machine points = 48.486309. This is one practice-board run,
not the final five-run holdout result. No board-measured earlier version exists.

Next candidate: mapped input, six parsing workers, compact records, one contiguous
latency array, exact selection and buffered parallel formatting. Full-size seeds
1 and 2 match the reference. Local full-seed-1 runtime is about 0.25-0.40 s on
the native WSL filesystem; these timings are not the scoring machine. Board
measurement pending; compare against run-9b28b732aad3.

Board confirmation for commit 3cb973f: run-f0f7b72f46d4, all gates passed,
T=0.161052 s, M=52,199,424 bytes, S=249, K=15.144617, points=59.012914.
This is 4.36x faster than the first recorded board baseline, with more memory
and source lines; K improved from 9.326661 to 15.144617.

Next: profiling found repeated serial endpoint lookup consumed about 0.10 s
locally. Resolve each worker's endpoint once for all 72 hours, use a flat exact
endpoint table and a short-window hash with full byte collision checks. Local
merge time fell to about 0.02-0.03 s. Both full seeds still match.

Board confirmation for cf4fee3: run-6c8468e0b768, all gates passed,
T=0.099913 s, M=53,575,680 bytes, S=300, K=18.600802, points=63.476583.
This is 7.03x faster than the first board baseline. The current build adds LTO
and profile guidance from a build-time seed-17 sample. Full seeds 1/2 still pass;
the profile-guided candidate awaits its own board measurement.

PGO receipt for dde1d70: run-d09c4f1336cc, all gates passed,
T=0.107640 s, M=53,399,552 bytes, S=306, K=17.879354, points=62.617591.
This trial regressed against cf4fee3; compiler guidance alone was not a speed win.

Next candidate: lazily allocate per-hour counter arrays, intern endpoint names
before sorting, and prepare endpoint references/prefix ranges in parallel.
Twelve workers are selected for the scoring machine. Native full-seed-1 runs
were approximately 0.11-0.18 s with about 279 MB RSS including mapped input.
Full seeds 1/2 pass. SIMD space scanning was correct but had only a marginal
local effect and was omitted. Official measurement of this layout is pending.

Layout receipt for 9e77b66: run-d18fa4b05fd8, all gates passed,
T=0.064404 s, M=48,898,048 bytes, S=343, K=23.125735, points=68.204778.

Next candidate appends directly to per-worker group buckets, uses three inline
values and lazily allocated 64-group pages, and writes hour buffers without a
joined copy. Full seeds 1/2 pass. Native full time was roughly 0.12-0.16 s and
270-271 MB RSS including mapped input. A compressed temporary-record variant
was correct but was superseded by this direct grouping layout. Compiler training
now uses the full seed-17 scale; local offline build and full public hash pass.
Official time/memory/source tradeoff remains to be measured.
