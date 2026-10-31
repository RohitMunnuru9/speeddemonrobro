# Optimization log: C

Implementation began October 31, 2026, during the event window.

Version 1: parse CSV bytes directly, use a dense integer region lookup, and
aggregate exact integer cents and exact unescaped name sets. Local correctness
and timing evidence is recorded by tools/verify.py at repo root.

No board-measured version 1 exists; its results are local only.

Version 2: intern each exact unescaped name once with a country membership mask,
count note bytes without storing notes, and mmap the CSV read-only. On full seed
1 locally, RSS fell from 437,504 KiB to 213,732 KiB. Both versions matched the
published output hash. Wall time on the OneDrive mount varied substantially,
so no speed ratio is claimed from those runs.

First board baseline (version 2): `run-ca1317b2135c`, commit
`b7b22498b87bbe3012f23fc16f5f4fb0e09b04d2`, full public seed 1.
All gates passed. T = 0.972772 s, M = 14,352,384 bytes, S = 188, D = 1,
K = 6.782354, machine points = 41.569023. This is one practice-board run,
not the final five-run holdout result. The local RSS numbers above include
mapped input pages; the board's cgroup memory measurement is the scoring axis.

Next candidate: six CSV workers, allocation-free field views, and one shared exact
name table with atomic membership bits. Tested with full seeds 1 and 2 and a
60,000-record duplicate-name stress case spanning all workers. Local full-seed-1
runtime is about 0.32-0.40 s, versus roughly 2.35 s for the prior implementation
on native WSL storage. Board measurement pending; compare against run-ca1317b2135c.
