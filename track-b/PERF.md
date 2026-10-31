# Optimization log: B

Written October 31, 2026, during the event window. This fourth entry is a candidate
for the best-three overall score; board registration/measurement is pending.

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

No board run ID or official improvement is claimed yet. Every index is built
from the runtime input, and both query and corpus bytes can vary with the seed.
