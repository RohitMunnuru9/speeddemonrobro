# Optimization log: D

Implementation began October 31, 2026, during the event window.

Version 1: CSR adjacency arrays, reusable per-worker search buffers, exact
indegree/outdegree shortcuts and six independent query workers. Local correctness
and timing evidence is recorded by tools/verify.py at repo root.

First board baseline: `run-d1b7b2ec0eeb`, commit
`b7b22498b87bbe3012f23fc16f5f4fb0e09b04d2`, full public seed 1.
All gates passed. T = 1.836254 s, M = 214,720,512 bytes, S = 143, D = 1.
K = 6.986926, machine points = 42.214307. This is one practice-board run,
not the final five-run holdout result. No board-measured earlier version exists.

Next candidate: mapped two-pass graph construction, forward/reverse CSR,
bidirectional Dijkstra and radix heaps. Expanding the smaller frontier was faster
locally than selecting a direction by distance. Both full seeds and 9,000
independent Bellman-Ford query answers match. Local full-seed-1 time is roughly
1.74-2.06 s, down from about 5.1 s on native WSL storage. Board measurement pending;
compare against run-d1b7b2ec0eeb. Graph parsing and preprocessing remain timed.
