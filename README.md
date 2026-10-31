# Speed Demon entry

Four independent C++20 entries: A Ingest, B Index, C Join and D Traverse. Each track
builds offline and has no nonstandard runtime dependencies. MIT licensed.

The organizer and starter checkouts are local reference material, ignored by Git.
Only each `track-*` folder is a Docker build context.

## Local verification

On Linux with GCC 15 and Python 3.11 or newer:

```
git clone https://github.com/hackathon-raptors/speed-demon-2026 organizer
python3 tools/verify.py --scales tiny sample full --seeds 1 2
```

The script compiles every entry, compares complete output SHA-256 hashes against
the official reference on the worked examples and generated inputs, and records
local wall time and process RSS in `.results/local.json`. Those numbers are not
board scores or cgroup memory measurements.

## Submission setup

Use the `speed-demon` branch in one GitHub repository. Tracks A, C and D are
registered and scored. Track B is a validated fourth candidate awaiting its
registration update. Install the Speed Demon Board app on the repository.
The team field is `vayu`; the submission repository is
https://github.com/RohitMunnuru9/speeddemonrobro.
Make the repository public under the included MIT license by code freeze.

All three tracks passed their first organizer board run on October 31, 2026.
Each track's PERF.md records its exact run ID and measurements. Local measurements
and practice-board results are distinct from final holdout scoring.
