# A streaming experiment

The submitted candidate is the C++ implementation in track-a. experimental.rs
preserves the independent Rust version of the same one-pass/retry architecture;
that Rust candidate was checked against the full public hash, but is not the
submitted executable. B, C and D were not changed for this trial.

cpp-stream-pairs.txt contains eight warm actual-PGO comparisons after a first
warm-up. The reused harness labels map as follows:

- current: retained ee47856/e71d607 Rust A.
- keep: the new Rust shared-catalogue streaming version, 384 pieces.
- release: the new C++ streaming PGO binary, 96 pieces. This name is only a
  harness alias; this candidate does not unmap input pieces during parsing.

C++/retained-Rust median wall ratio: 0.67584; CPU ratio: 0.73601. Every run's
complete output hash matches. These four-CPU local timings are not official
score predictions. validation.txt records independent graph-free A checks;
catalog-asan.txt and catalog-tsan.txt record the catalogue race/overflow tests.
The C++ candidate's official receipt belongs in track-a/PERF.md.
