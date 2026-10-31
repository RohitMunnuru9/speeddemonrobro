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

Inline-adjacency receipt for 74f2b9b: run-bac560020c72, all gates passed,
T=0.440488 s, M=183,824,384 bytes, S=377, K=12.724149, points=55.231438.
This regressed against the earlier CSR result and was replaced.

Next candidate parses edges once into coarse forward/reverse buckets, with
lossless packed temporary records and exact overflow for capacity estimates.
Bucket owners produce CSR using small local degree arrays. Ordinary final arcs
occupy four bytes; a tagged exact exception array handles large weights/nodes.
Node and weight widths derive from the current graph. Distances remain uint64,
and the prior bidirectional sparse-state/radix-heap search is retained.

Algorithm inspiration, independently implemented here:
https://github.com/ahptour/friday-oct-30-2026/blob/speed-demon/track-d/PERF.md
We read its public account of owner-routed CSR and narrow/wide arc storage,
then implemented our own bucket capacity/overflow and construction pipeline.
Its benchmark numbers are not claimed as ours.

The compact-CSR candidate passes complete full seed 1/2 hashes, independent
full seed 17, official edge cases and 9,000 arbitrary-graph answers compared
against an independent Bellman-Ford oracle. Zero-edge graphs are also checked.
Native RSS including mapped input fell to approximately 246 MB; development
wall times varied substantially with load, so the board decides the tradeoff.
The actual offline image passes the sealed full public hash check. Official
measurement of this candidate is pending.

Compact-CSR receipt for a64a529: run-de9af6b930ae, all gates passed,
T=0.297302 s, M=139,333,632 bytes, S=441, K=15.990120, points=60.192587.
Compared with the earlier best CSR run, time fell 28.4% and peak memory 20.9%.
A compact binary-heap experiment was correct on the public full hash but slower
in paired local runs and was rejected; radix heaps remain in production.

Next candidate retains only forward temporary edges and counts reverse degrees
atomically during parsing. After forward CSR completes, its temporary buckets
are freed and reverse CSR is filled by parallel transposition with atomic cursors.
This exchanges some construction time for a much smaller peak working set.
Meeting checks move from every relaxation to settled vertices, and search work
is balanced by discovered-node counts. The min-sum stopping condition is unchanged.

Local paired runs: transpose median 0.89 s versus 0.805 s for the dual-temporary
version, with RSS approximately 191-193 MB versus 246 MB including mapped input.
Later candidate checks recorded approximately 176-177 MB; file residency and
system load make the official peak essential. Both full public hashes and all
9,000 independent graph answers pass. Packed search-state and packed radix-item
experiments did not show a reliable sufficient gain and were rejected. Official
time/memory/source tradeoff of transposition remains to be measured.

Transpose receipt for 3292dcb: run-9fe8cfb181d0, all gates passed,
T=0.482704 s, M=102,223,872 bytes, S=472, K=13.215555, points=56.054271.
Peak memory fell, but time regressed sharply on the scoring box. Restore the
dual-bucket CSR builder and original frontier/meeting policy from a64a529,
whose K was 15.990120. This trial is not retained.

Further local trials of a small membership filter and aggressive query-table
shrinking did not establish a sufficient reliable end-to-end gain. They remain
outside the submission. The retained runtime/build sources match the previously
scored stronger configuration; experimental receipts are preserved above.

Next candidate combines the two directional search-state tables. Each entry
holds both uint64 distances, so a relaxation returns its opposite distance
without a second hash probe. Stale-heap checks likewise read both values in one
lookup. A shared epoch and exact node comparison retain the original Dijkstra
and stopping rules. Temporary bucket reserve drops from 20% to 5% slack, with
the existing exact overflow preserving every edge under skewed counts.

Three alternating native runs of the combined-state change before reserve
tuning gave median 0.5981 s against 0.6385 s for the prior source (about 6.3%
lower). Further packed-arc and membership-filter variants did not establish
a sufficient gain and were not selected. Both public full hashes and
tiny/sample seeds 1/2 pass. Official measurement is pending.

Combined-label receipt for 4539ae4: run-044bd9c8969e, all gates passed,
T=0.315156 s, M=128,053,248 bytes, S=447, K=15.763062, points=59.882029.
The smaller build peak did not offset slower search. The combined table is
rejected in favor of the previous two directional tables.

Next candidate retains the 5% reserve slack and adds an exact membership filter
only before opposite-direction lookups. Filter collisions always fall through
to full node/distance checks. A bitmask tracks occupied positive radix buckets,
so finding the next bucket uses a trailing-zero operation instead of a scan.
Distances remain uint64 and search/termination policy remains unchanged.
Three alternating local runs gave median 0.9108 s against 1.0280 s for the prior
best source (about 11.4% lower). Official measurement remains pending.
Both full public seeds, the full independent seed 17, the sealed Docker run,
and 9,000 independent arbitrary-graph answers pass for this candidate.
