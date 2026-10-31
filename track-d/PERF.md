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

Board confirmation for commit 3cb973f: run-204ce24376f8, all gates passed,
T=0.849999 s, M=266,108,928 bytes, S=265, K=8.968497, points=47.635982.
This is 2.16x faster than the first board baseline, but reverse adjacency and
worker buffers increased M. The new K is the improvement criterion.

Next candidate builds CSR through parallel stable radix ordering instead of
random scatter, and packs exact distances plus generation tags into one word.
The distance bit width follows (n-1)*UINT32_MAX and generation wrap clears tags.
Both full seeds and independent graph tests pass. Board measurement pending.

Board confirmation for cf4fee3: run-1b7777c4fd9b, all gates passed,
T=0.557152 s, M=210,890,752 bytes, S=318, K=11.291853, points=52.638260.
The current build adds LTO/profile guidance from a generated seed-17 sample.
Local four-CPU Docker medians for 4/6/12 query workers were 0.651/0.623/0.679 s,
with local cgroup peaks 210.9/267.6/438.7 MB. Four workers retain the best local
time-memory tradeoff, and remain the default. Official PGO measurement is pending.

PGO receipt for dde1d70: run-38704b25b6ff, all gates passed,
T=0.550362 s, M=210,796,544 bytes, S=324, K=11.330497, points=52.712448.

Next candidate replaces dense node-sized distance/generation vectors with exact
flat query-state tables. The table grows with visited nodes and uses uint64
distances, so it has no distance-packing restriction. Native full times were
about 1.09-1.21 s, with 263-264 MB RSS including mapped input. Full seeds 1/2
and independent arbitrary-graph checks pass. Local Docker cgroup peaks remained
about 176.8 MB for 4, 6 and 12 workers, dominated by graph construction. We select
12 workers for an official trial; local four-CPU times do not establish a
speed gain on the scoring box. The packed-state code remains in Git history.

Sparse-state receipt for 9e77b66: run-3569b54af56f, all gates passed,
T=0.415448 s, M=176,226,304 bytes, S=366, K=13.271896, points=56.146649.
Twelve workers improved time 24.5% against dde1d70 while lowering peak memory.

Next candidate stores the first three arcs inline per node, retaining exact
overflow for higher degrees, and lowers query-table load to at most 50%.
Native full runs were roughly 0.99-1.07 s, with about 269 MB RSS including mapped
input. Full seeds 1/2 and independent graph tests pass. A forward-only inline
variant was correct but took about 1.83-2.03 s and was rejected. Compiler training
now uses full seed-17 data; the offline build and full public hash pass. Board pending.

Rejected local experiments: BFS vertex ordering reduced query time but added too
much preprocessing (roughly 0.9-1.1 s build); degree-two contraction reduced full
nodes from 1,740,512 to 1,342,958 but increased total time; pruning unqueried
bidirectional leaves also failed to repay preprocessing. Unidirectional radix
search used less memory but was slower end to end. These variants are not retained.
