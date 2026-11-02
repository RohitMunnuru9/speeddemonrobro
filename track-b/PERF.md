# Optimization log: B

Written October 31, 2026, during the event window. This fourth entry is a candidate
for the best-three overall score; all four tracks are registered.

1. First correct query-aware posting-list implementation passed the official
worked example, tiny and sample seeds 1/2, and the published full seed-1 output
SHA-256. Native WSL full runs took about 4.8-5.1 s, with roughly 457 MB RSS.
2. AVX2 token classification and a flat term table reduced local full time to
about 2.8-3.1 s. All full output hashes remained identical.
3. Hybrid list/bitmap boolean operations and weighted-popcount output avoid
materializing large result sets. Choosing dense terms from a runtime corpus
sample reduced retained postings and removed bitmap write atomics by aligning
worker boundaries to bitmap words.
4. Exact zero-padded 32-byte token keys replace scattered string comparisons.
Local full runs are about 2.48-2.59 s, with roughly 390 MB RSS (including mapped
input pages). These local numbers are not cgroup memory or board scores.

Published full output hash:
f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927

First board receipt, cf4fee3: run-8a003e6e528d, all gates passed,
T=0.825134 s, M=143,810,560 bytes, S=536, D=1, K=10.929498,
points=51.930011. This is the official baseline for subsequent changes.

Current candidate: exact 16-bit block-relative postings, a counting/filling
allocation strategy, lazy segmented views, and a runtime-selected short-token
cache. Full public output matches the published hash; native full runs were
about 1.41-1.43 s. An LTO/profile-guided build trained on a generated seed-17
sample had local Docker process medians of 1.167 s (6 workers) and 1.125 s
(12 workers), with local cgroup peaks about 88 MB. The prior board baseline
used 143.8 MB officially; these different environments are not directly
comparable. Twelve workers are selected for the next official trial.

Rejected runtime rare-anchor/DNF planner: full output stayed correct, but
rechecking raw text raised native full time to 2.92-3.19 s. It is not retained.
Every index is built from the runtime input, and corpus/query bytes can vary
with the seed. No precomputed index or answer file is included.

Compact-posting/PGO receipt for dde1d70: run-35580c12514b, all gates passed,
T=0.798123 s, M=88,948,736 bytes, S=646, K=11.895908, points=53.769880.
Official memory fell 38% against cf4fee3; time improved only about 3%.
The next small change skips empty bitmap words during output aggregation.
A pooled one-pass index and an all-indexed DNF query rewrite were correct but
showed no clear end-to-end local gain; neither is retained.

Empty-word receipt for 9e77b66: run-a8ff69f0ea53, all gates passed,
T=0.726549 s, M=88,817,664 bytes, S=648, K=12.465990, points=54.786339.

Next candidate uses one-pass estimated reserve buffers with exact overflow,
non-overlapping AVX2 token classification, a blocked Bloom rejection filter,
vectorized count/sum aggregation, and byte positions for selected phrase anchors.
No estimate controls correctness. Full public output, tiny/sample seeds 1/2,
32-byte SIMD boundary cases, >65,535-byte documents and reserve-overflow cases
are validation gates. Local native full runs were around 1.3-1.5 s and 370 MB
RSS including mapped input; extra reserve/position memory must earn its cost on
the board. Training now uses full seed-17 data. Larger direct hot-token caches
showed little benefit and were not retained. Official measurement is pending.

Retained-layout receipt at f7fa9d4: run-37ee293238cc, all gates passed,
T=0.561159 s, M=136,093,696 bytes, S=834, K=12.540356, points=54.915493.

Next candidate enumerates start/end bitmaps over paired classification blocks
instead of repeatedly shifting/reloading masks for each word. A cache-line-sized
hot entry compares all 32 key bytes, extending runtime-sampled cache coverage
to long words. Dense ordered IDs permit reading the document count from the
last line, then constructing views through line-aligned parallel scans. Every
ID and text still comes from the supplied input.

Three warm alternating native runs before the document-scan change gave median
0.8010 s against 0.8548 s for the prior source (about 6.3% lower). A larger hot
cache was not faster and was rejected. Public full output and tiny/sample seeds
1/2 remain byte-exact. Official time, memory and source tradeoff is pending.

Receipt for 4539ae4: run-5c282078959e, all gates passed,
T=0.451909 s, M=137,101,312 bytes, S=866, K=13.875027, points=57.111692.
This is 19.5% faster than the previous best practice run, and gains 2.20 points.
The full independent seed-17 reference also matches exactly.

Next candidate passes typed 32-byte keys directly through token callbacks,
dictionary lookup and KMP verification. It removes intermediate byte-view/key
copies while retaining identical ASCII boundaries and padding. Three alternating
local runs gave median 1.2411 s versus 1.2788 s (about 3.0% lower). A bitmap-tile
experiment and fused per-worker metadata did not establish a sufficient reliable
gain after their costs and were not selected. Official measurement is pending.

The selected tokenizer also streams 64-byte classification blocks directly from
the input. A bounded 128-byte stack tail protects the last SIMD loads. Token
bytes are lowercased as keys are emitted, so the worker no longer constructs a
whole-document lowercase buffer and mask array first. Cross-block words, exact
32-byte words, final words and early callback termination retain their semantics.

Actual optimized PGO binaries were compared over six alternating warm runs:
baseline median 0.85267 s, typed callbacks 0.82634 s, streaming callbacks
0.82531 s. Absolute local timings are noisy. Streaming also reduces source and
temporary storage. Prefix-only hashes, removing Bloom guards, larger positional
coverage and an associative short-word cache failed to establish worthwhile
gains and were rejected. Official measurement of the selected trial is pending.

The selected source passes tiny/sample seeds 1/2, every tokenizer boundary and
posting-overflow edge case at 1/6/12 workers, the sealed full-public Docker run,
and the actual PGO executable against independent full seed 17
(87c77cf18a8841688a551061db6253f7d542609e8f82c69cfe756eb7f8a463be).

Receipt for adb4712: run-09e52f290c70, all gates passed,
T=0.422194 s, M=136,933,376 bytes, S=849, K=14.401287, points=57.920065.
Previous best: T=0.424208 s, K=14.348427, points=57.840215. Runtime is nearly
unchanged; smaller source offsets slightly higher memory. This is a small K
gain and does not establish a substantial speedup.

November 2 final trial: overlap eight exact token lookups during index creation.
The sampled hot-key cache still compares all 32 initialized key bytes. Other
tokens carry a precomputed hash, and prefetch the Bloom word and dictionary
home slot before retirement. Bloom positives and hash collisions still require
the original exact key comparisons. Retirement preserves token order and drains
at each document boundary, preserving sorted positional postings and duplicate
suppression. The pending array is allocated once per worker, not per document.

Eight warm adjacent actual PGO pairs against the submitted streaming tokenizer
gave candidate/current median wall ratio 0.94773, CPU ratio 0.95435 and process
peak-RSS ratio 0.99996. Wider 16/32-token windows were slower and are excluded.
These are four-CPU development measurements, not predicted official scores.
Public full output and tokenizer/posting edge cases pass; final sealed checks
and the official receipt are recorded with the final submission.

The final offline, unprivileged, network-sealed full-public runtime matches
f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927.
Its actual optimized binary also matches the saved independent full seed-17
reference, 87c77cf18a8841688a551061db6253f7d542609e8f82c69cfe756eb7f8a463be.
