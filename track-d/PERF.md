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

Occupancy/filter receipt at ee47856: run-f1a242f9b1dc, all gates passed,
T=0.300736 s, M=129,384,448 bytes, S=457, K=16.049853, points=60.273554.
This slightly improves the prior best K=15.990120; time is similar and memory
is lower. It does not establish a large speed gain on the scoring box.

Next candidate packs three encoded four-byte arcs and the overflow range into
one 16-byte node record. It preserves owner-routed construction, exact large-
weight exceptions, unrestricted degree overflow and uint64 path distances.
The first neighbor no longer requires a separate adjacency read, and high-degree
overflow is prefetched before processing inline arcs. This differs from the
earlier rejected 32-byte-node layout, which stored eight-byte arcs.

Three alternating native runs gave median 0.9310 s versus 1.2135 s (about 23.3%
lower) with the full public output unchanged. Official memory and time must
both improve K before retaining this layout. Official measurement is pending.

The next trial also halves ordinary query-state entries to eight bytes. Node
bits come from the input; the remaining low-word bits hold a generation and a
wide-distance tag. Ordinary distances occupy the high word. Larger distances
use an exact uint64 side array, with its index in that word. Rehash copies
current encoded entries; generation wrap clears stale records before reuse.
Full-width radix heaps remain unchanged: a separate packed-heap experiment did
not improve runtime and was rejected.

Three alternating native runs gave 0.42427 s versus 0.44258 s for 16-byte labels
(about 4.1% lower). Public full seeds 1/2, tiny/sample seeds 1/2 and 9,000 arbitrary
graph answers pass. Another 2,054-answer chain test forces state rehash, distances
above uint32 and a generation wrap on one worker. Construction still determines
peak local RSS, so halving query labels does not imply halving total memory.
Official time/memory/source tradeoff is pending.

The combined inline-node/packed-label candidate also passes the actual sealed
full-public Docker run and its PGO executable matches the independent full
seed-17 reference (ad7c211024014a4c21a50faab598dd19e730cbc0b4e8013a7d3b3306132913ba).

Receipt for adb4712: run-16272588120f, all gates passed,
T=0.232462 s, M=124,387,328 bytes, S=490, K=18.208199, points=63.013350.
Against the prior best at ee47856, wall time fell 22.7%, memory fell 3.9% and
K rose 13.4%. The combined inline-node/packed-label layout is retained.

November 1: D-first optimization. A Rust search engine was built against the
same C++ graph arrays and checked against the full public hash. Seven alternating
unprofiled native runs gave medians 0.45182 s (Rust) and 0.43886 s (C++). This
does not establish a Rust advantage, so the measured C++ implementation remains.

Chain contraction, graph relabelling, landmark A*, parallel landmark setup,
dense/shared labels and alternative queues all matched the public full output
but did not establish an overall gain after preprocessing, source and memory
costs. They are not part of the selected submission.

The selected change overlaps graph/label reads with queue work. Radix
redistribution prefetches vertices at its new minimum key. A worker then removes
up to sixteen entries already in that zero bucket and prefetches their graph
records and own-direction label slots before expansion. It does not inspect a
future bucket during collection, which would advance the monotone heap before
the current batch can insert zero/small-weight relaxations. Stale entries are
still checked individually; the original distance and stopping rules remain.

Five adjacent pairs of actual optimized PGO binaries gave a median candidate /
baseline wall-time ratio of 0.62066 and CPU-time ratio of 0.62058. This is about
38% lower local time, not an official measurement or a 90% improvement. The
development machine was noisy, so each candidate was compared with an adjacent
baseline run. Wider filters, split key/node queue storage and huge-page hints
were not consistently better and were left out of this trial.

Tiny/sample/full seeds 1/2 match, as do 9,000 independent arbitrary-graph
answers and the wide-distance/generation-wrap stress test. The queue also passes
233,480 differential reference pops with wide keys, ties and resets. The actual
sealed image passes full public seed 1, and its PGO executable matches the
independent full seed-17 reference. Official measurement is pending.

Receipt for d00f34a: run-5de48c7f21fb, all gates passed,
T=0.202663 s, M=128,188,416 bytes, S=519, K=19.217440, points=64.184777.
Against the preceding best T=0.232462 s, official runtime fell 12.8% and
points rose from 63.013350. This is smaller than the local paired gain.

Further November 1 trials: a Rust search engine with the same prefetch batches
and C++ graph builder passes the six public datasets, 9,000 independent graph
answers and the wide-label/generation-wrap test. Nine paired PGO runs gave
Rust/C++ median wall ratio 1.06865 and CPU ratio 1.09415. It is preserved in
experiments/rust-d, not selected. Two-pass direct graph layouts saved roughly
18% local peak RSS but were 32–33% slower; a wider-digit heap was essentially
tied. Direct chain walking was slower, and pruning paths above the current
complete-path upper bound was essentially tied. These trials are not submitted.

Receipt for 1589306, with unchanged D code: run-3fb03ac0d003, all gates passed,
T=0.188206 s, M=130,605,056 bytes, S=519, K=19.867526, points=64.907190.
The lower runtime is run variation, not evidence of another D code improvement.

Next D-first trial retains owner-routed construction but uses six bytes for
ordinary temporary rows. Local/opposite IDs and 15-bit weights fit below an
escape flag; larger weights retain the original exact uint64 row in a side
array. When a coarse bucket fills, every existing packed row is decoded into
the exact growing overflow. Both forms expose the same logical row interface.

Search now prefetches own-direction label slots for inline neighbors after
prefetching the batch's node records. Seven adjacent actual PGO comparisons of
this combined design gave median wall ratio 0.98493, CPU ratio 1.01663 and
process peak-RSS ratio 0.89638. RSS includes mapped input and is not the scoring
cgroup memory axis. A formatted range-view implementation also matches the
public full output and gave a 0.9556 native wall ratio in a shorter comparison.
This is a time/memory/source tradeoff requiring an official K comparison.

Neighbor prefetching alone gave PGO wall ratio 0.94863; compact storage alone
gave 1.04155. Prefetching opposite labels or every overflow neighbor was weaker.
Specializing the edge decoder did not establish an additional gain.

The selected combination passes public tiny/sample/full seeds 1/2, the 9,000
independent graph answers and the 2,054-answer wide-distance/generation test.
A new concentrated-star test checks 184 answers across one/twelve search
workers while all six parser workers overflow their coarse buckets with mixed
ordinary, escaped and UINT32_MAX-weight rows.

The actual sealed image passes full public seed 1. Its optimized PGO executable
also matches the independent full seed-17 reference
(ad7c211024014a4c21a50faab598dd19e730cbc0b4e8013a7d3b3306132913ba).

Receipt for the combined trial at 7888cf2: run-68ef5bde6114, all gates passed,
T=0.200244 s, M=107,827,200 bytes, S=567, K=19.749893, points=64.778237.
Memory fell 17.4% against the preceding best, but the combined score did not
beat 64.907190. Revert compact temporary storage and compare neighbor-label
prefetching alone, which was faster in the paired PGO comparison and adds
only the prefetch stage. The measured compact version remains in git history.

The prefetch-only e71d607 trial passed all gates in run-34fb486d6c0e:
T=0.191793 s, M=127,545,344 bytes, S=532, K=19.701150, points=64.724579.
This did not exceed the preceding best K=19.867526.

The next trial groups four label probes in a bounded AVX2 load. Three mirrored
slots after each power-of-two table make wraparound loads valid. Low-slot
writes update their mirrors; rehash copies only real entries and refreshes
mirrors. Full node/generation comparisons, exact uint64 wide labels and the
membership filter remain unchanged. No deletion occurs within a query, so a
matching key cannot lie past an earlier empty slot in its linear probe sequence.

Remove the opposite-label check at pop: every successful label improvement
already checks the opposite direction, so the latest update of either label
has evaluated that pair. Source==target is handled before queue setup. The
stopping rule and real directed-path candidates are unchanged.

Eight warm adjacent actual PGO pairs gave vector/baseline median wall ratio
0.94724 and CPU ratio 0.93640. The first pair is excluded because the final
candidate build was still finishing. A narrow uint32 queue with an exact
uint64 restart fallback gave 0.97843; combining it with vector labels gave
0.95586. Neither narrower variant is selected in this trial.

The vector table passes 783,636 independent reference-map updates exercising
growth, wraparound probes, wide values and generation reuse. Disabling mirrored
writes makes that oracle fail. Public seeds 1/2 at all three scales, the graph
edge suite, wide-distance/generation stress and the bucket-migration star pass.
The sealed full-public image passes; its PGO executable matches the existing
independent full seed-17 reference. Official performance is pending.

November 2 final trial: dbc9cb4 passed all gates in run-6345e74459c2,
T=0.195748 s, M=130,674,688, S=547, K=19.326026, points=64.307128.
It did not exceed the retained 64.907190 best.

Replace the search radix heap with an exact 1,024-distance circular window.
Window slots contain linked lists in one reusable arena; popped links return
to a free list. Keys outside the window remain full uint64 entries in an
ordered heap. The next minimum compares both representations before advancing,
then promotes only keys fitting the new window. This includes the far-only
UINT64_MAX case. Equal-key batches stop before advancing the queue, preserving
monotone insertion even with zero-weight arcs. Move rare table growth and wide
label encoding into cold helpers; full-key comparisons and mirrors remain.

Eleven adjacent actual PGO comparisons against dbc9cb4 gave median wall ratio
0.94921, CPU ratio 0.93122 and process peak-RSS ratio 1.00099. Keeping persistent
construction workers was less convincing after PGO and is excluded. BFS-based
graph relabeling was slower after including its setup and is also excluded.

A new independently implemented Rust arena-queue solver was tested against the
same immutable C++ graph builder. Its first unprofiled comparison was 13% faster,
but actual PGO comparisons were approximately tied (roughly 0.97 to 1.00 wall
ratios across trials), with more formatted source. Unchecked hot accesses and
four-lane mirrored probes did not establish a further gain. Rust is preserved
under experiments/rust-d, not selected for the submitted track. Its independent
Rust oracles pass 1,408,200 label updates and 233,724 queue pops, including wide
keys, wraparound, generation reuse and UINT64_MAX. The regular Rust graph solver
also passes the arbitrary-graph, wide-distance and concentrated-star suites.

The selected C++ queue passes 233,480 independent queue pops, 783,636 label
updates, 9,000 Bellman-Ford graph answers, 2,054 wide/generation answers and
184 packed/wide/migration star answers. Local full-public output is exact.
The official result is still pending; no 80-point result is claimed.

The final offline, unprivileged, network-sealed runtime matches full public
seed 1, a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e.
Its actual optimized binary also matches the saved independent full seed-17
reference, ad7c211024014a4c21a50faab598dd19e730cbc0b4e8013a7d3b3306132913ba.
