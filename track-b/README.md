# B: Index

Queries are read first and identical query lines are computed once. Only their
terms are indexed. Tokens use exactly ASCII letters/digits after ASCII lowercasing;
non-ASCII bytes are separators. AVX2 classification supplies zero-padded 32-byte
keys, and the exact key comparison checks all bytes rather than trusting a hash.

A runtime sample selects dense bitmaps for common terms and sorted posting lists
for rare terms. This affects representation only. Six workers index disjoint
document ranges aligned to 64-document boundaries, so bitmap word ownership is
exclusive and writes need no atomics. Sparse posting lists are merged in document
order and contain each document once per term.

Boolean operators execute strictly left to right. Mixed bitmap/list operations
preserve set semantics. Bitmap output counts and sums use weighted popcounts;
only the first ten set bits need enumeration. Phrase candidates start with the
rarest term, check other memberships, then use exact token-level KMP on raw text.
Stopwords, repetitions, punctuation and cross-sentence matches are retained.

There are no external dependencies or precomputed indexes/answers. All sampling,
indexing, queries and output occur in the timed run. The Dockerfile targets the
scoring machine's AVX2-capable x86-64 CPU.

With another 24 hours: improve the term table's cache layout and shrink temporary
posting allocations, then compare positional anchors with re-tokenization costs.
