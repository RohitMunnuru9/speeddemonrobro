# B: Index

Queries are read first and identical query lines are computed once. Only their
terms are indexed. Tokens use exactly ASCII letters/digits after ASCII lowercasing;
non-ASCII bytes are separators. AVX2 classifies non-overlapping 64-byte blocks
and constructs exact zero-padded 32-byte keys directly from the input. A small
blocked Bloom filter skips definite dictionary misses; all possible matches
still compare exact keys, so false positives cannot change answers.
Typed word keys pass directly from tokenization to dictionary lookup and phrase
verification, avoiding intermediate byte views and repeated key copies.

Document count comes from the supplied final dense document ID. Line-aligned
parallel scans then populate document views by their actual IDs. Token starts
and ends are enumerated from paired 32-byte classification masks, including
words crossing a block boundary and the final word. A bounded stack tail
supports safe SIMD loads. Folding occurs only for emitted tokens, without a
second lowercase copy or a whole-document array of classification masks. Query
verification can stop tokenization immediately after a confirmed match.

A runtime sample selects dense bitmaps for common terms and sorted posting lists
for rare terms. This affects representation only. Twelve workers index disjoint
document ranges aligned to 64-document boundaries, so bitmap word ownership is
exclusive and writes need no atomics. Sparse postings use exact 16-bit document
offsets within each worker block. A sample estimates per-term space in one buffer
per worker. If any estimate is too small, that term migrates to an exact growing
overflow list. This permits one corpus pass without dropping any posting.
Segmented views yield global IDs in document order without copying the lists.
Each document appears once per term. A small
runtime-selected cache accelerates frequent complete token lookups with exact
32-byte keys, including words longer than eight bytes.

Boolean operators execute strictly left to right. Mixed bitmap/list operations
preserve set semantics. Bitmap counts/sums use AVX2 nibble lookups and exact
64-bit accumulation; only the first ten results need enumeration. Selected
phrase-anchor terms retain every occurrence's byte position. Verification walks
the neighboring tokens directly and compares every byte. Other phrases and
documents over 65,535 bytes use exact token-level KMP after membership filtering.
Stopwords, repetitions, punctuation and cross-sentence matches are retained.

There are no external dependencies or precomputed indexes/answers. All sampling,
indexing, queries and output occur in the timed run. The Dockerfile targets the
scoring machine's AVX2-capable x86-64 CPU.

Compiler profile guidance uses full-size generated seed-17 data at build time. Only
the executable is shipped. SD_THREADS can select 1-12 workers; the minimum rises
when necessary to preserve the exact 16-bit block bound.

With another 24 hours: tune reserve slack and positional-anchor selection against
official time/memory measurements, and reduce common-query bitmap traffic.
