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
