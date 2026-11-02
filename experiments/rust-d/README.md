# Experimental Rust D solver

This is an independently implemented, std-only Rust search engine over the
production C++ graph builder. It is not linked by the submitted Dockerfile.
`build_driver.py` emits the matching C++ frontend from the current track.

`lib.rs` uses an exact circular distance window, a recycled-link arena, a full
uint64 far heap, generation-tagged exact labels, and bidirectional Dijkstra.
`arena-vector.rs` additionally probes four labels with AVX2 and maintains three
mirrored end slots. Neither uses fingerprints as node identities or truncates
wide distances. Both keep C++ graph storage alive through every Rust worker.

The new regular solver initially beat unprofiled C++ locally by about 13%.
The actual submission build, with independently generated seed-17 training,
was approximately tied with optimized C++; unchecked-access and SIMD variants
did not establish a further gain. The C++ recycled-arena queue was about 5%
faster in the final optimized comparison and uses less formatted source, so it
was selected. Full measurements are in ../measurements/final-*-pairs.txt and
final-d-selection.txt; their reused harness labels are identified below.

In final-d-selection.txt, `current` is the submitted dbc9cb4 optimized C++,
`plain` is the Rust regular solver without Rust PGO plus a PGO C++ builder,
and `cold-arena` is the selected C++ arena queue with cold label helpers.
In final-rust-d-pgo-pairs.txt, `plain` is the regular Rust PGO solver and
`cold-arena` is the mirrored SIMD Rust PGO variant, both with a PGO C++ builder.

The independent Rust reference-map/ordered-queue tests pass 1,408,200 label
updates and 233,724 queue pops. They cover growth, wrap probes, wide values,
generation reuse, resets and a lone UINT64_MAX queue entry. The regular solver
also passes 9,000 arbitrary-graph answers, 2,054 wide/generation answers and
184 high-degree/mixed-weight answers. Both variants match public full seed 1.
These are validation results, not an official Rust score.
