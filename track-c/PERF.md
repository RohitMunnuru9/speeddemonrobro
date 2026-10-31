# Optimization log: C

Implementation began October 31, 2026, during the event window.

Version 1: parse CSV bytes directly, use a dense integer region lookup, and
aggregate exact integer cents and exact unescaped name sets. Local correctness
and timing evidence is recorded by tools/verify.py at repo root.

Board run ID: pending submission. No organizer timing or score claimed yet.

Version 2: intern each exact unescaped name once with a country membership mask,
count note bytes without storing notes, and mmap the CSV read-only. On full seed
1 locally, RSS fell from 437,504 KiB to 213,732 KiB. Both versions matched the
published output hash. Wall time on the OneDrive mount varied substantially,
so no speed ratio is claimed from those runs. Board confirmation remains pending.
