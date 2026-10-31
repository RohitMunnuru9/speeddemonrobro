# Speed Demon entry

Three independent C++20 entries: A Ingest, C Join and D Traverse. Each track
builds offline and has no nonstandard runtime dependencies. MIT licensed.

The organizer and starter checkouts are local reference material, ignored by Git.
Only each `track-*` folder is a Docker build context.

## Local verification

On Linux with GCC 15 and Python 3.11 or newer:

```
python3 tools/verify.py --scales tiny sample full --seeds 1 2
```

The script compiles every entry, compares complete output SHA-256 hashes against
the official reference on the worked examples and generated inputs, and records
local wall time and process RSS in `.results/local.json`. Those numbers are not
board scores or cgroup memory measurements.

## Submission setup

Use the `speed-demon` branch in one GitHub repository. Register that repository
and tracks A, C and D with the organizers and install the Speed Demon Board app.
The team field currently says `Rohit`; it must match the registered team name.
Make the repository public under the included MIT license by code freeze.

No board run has been recorded yet. Populate each track's PERF.md with actual
board run IDs as they become available. Never label local measurements as board
results.
