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
