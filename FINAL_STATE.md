# Vayu — current candidate status, November 2

Track A is a new C++ one-pass streaming candidate, awaiting official scoring.
It passed public seeds 1/2 at every scale, independent full seed 17, reordered
input/retry tests, exact-collision tests and catalogue sanitizer tests.

B, C and D remain on the exact runtime implementations of their strongest
verified configurations: adb4712 (B), 1589306 (C and D). The last verified
overall best-three machine score was 70.7270. An 80-point result is not claimed.

The A rollback is the Rust configuration at ee47856/e71d607, K=32.547366 and
75.625792 points. The current candidate must exceed that K to be retained.
Final scoring snapshots the branch tip; it does not assemble historical best
track versions automatically. Each PERF.md contains measurement receipts.
