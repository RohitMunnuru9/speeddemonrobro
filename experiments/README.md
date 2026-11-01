# Rust comparison candidates

These are measured experiments, not the submitted track implementations. They
have no third-party dependencies. The selected C++ versions remain in the track
directories because they were faster in this comparison.

`rust-c/main.rs` is a complete Linux Rust CSV solution: mapped input, scoped
workers, bounded AVX2 quote scanning and exact shared name tables with atomic
country masks. Slot ownership is claimed before tail writes; release/acquire
publication makes immutable tail bytes visible to other readers. Hash collisions
compare full keys and use an exact locked overflow map. SIMD reads are bounded
by the input mapping, not by the logical field length.

`rust-d/lib.rs` replaces the search engine with Rust while retaining the existing
C++ graph builder. This is a hybrid, not a complete Rust port. Immutable graph
arrays remain owned by the frontend until all scoped workers join. Each worker
claims distinct output slots. The packed label tables, full-width radix keys and
same-key prefetch batches retain the submitted algorithm's exact behavior.

Build on Linux from the repository root (Rust 1.99.1, GCC 15):

```sh
rustc --edition=2021 -C opt-level=3 -C target-cpu=x86-64-v3 -C panic=abort experiments/rust-c/main.rs -o /tmp/rust-c
python3 experiments/rust-d/build_driver.py /tmp/rust-d-driver.cpp
rustc --edition=2021 --crate-type staticlib -C opt-level=3 -C target-cpu=x86-64-v3 -C panic=abort experiments/rust-d/lib.rs -o /tmp/rust-d.a
g++ -std=c++20 -O3 -march=x86-64-v3 -pthread -I track-d /tmp/rust-d-driver.cpp /tmp/rust-d.a -ldl -o /tmp/rust-d
```

Both executables accept input and output directories, like the submissions.
`SD_THREADS=12` matches the submitted runtime environment.

November 1 measurements: nine adjacent pairs of optimized PGO executables,
trained on generated full seed 17 and tested on public full seed 1, gave Rust /
C++ median wall-time ratios **1.10117 for C** and **1.06865 for D**. CPU-time
ratios were 1.12513 and 1.09415. Lower is better. These are local comparisons,
not organizer measurements or evidence that Rust is intrinsically slower.

Both candidates match tiny/sample/full public seeds 1 and 2. The combined edge
suite passes 22 cases, including 9,000 independent Bellman–Ford graph answers,
CSV escapes, concurrent duplicates and forced hash collisions. D also passes
2,054 chain answers forcing wide labels, table growth and generation wrap.

Additional D graph-memory trials, alternative queue and chain-walking trials
did not establish a gain. Four-worker local results cannot justify reducing the
submission's twelve workers: the published runner uses CPU set 0–11, whereas
this development Docker VM has four CPUs.
