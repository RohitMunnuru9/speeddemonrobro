"""Compile changed entries and compare against cached official output hashes.

Cache test inputs on native Linux storage so Windows mount overhead does not
dominate the timing. Local timings/RSS are not organizer scores/cgroup memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from verify import ROOT, TRACKS, compile_entry

p = argparse.ArgumentParser()
p.add_argument("tracks", nargs="+", choices=list(TRACKS))
args = p.parse_args()
rows = []
cache = Path("/tmp/vayu-bench-inputs")
for t in args.tracks:
    binary = ROOT / ".build" / ("c-v2" if t == "c" else t)
    old = ROOT / ".build" / ("baseline-" + t)
    if binary.exists() and not old.exists(): shutil.copyfile(binary, old)
    compile_entry(t, binary)
    for label in ("tiny-1", "tiny-2", "sample-1", "sample-2", "full-1", "full-2"):
        filename = TRACKS[t][1]
        oracle = ROOT / ".results" / label / t / "reference" / filename
        if not oracle.exists():
            print(f"SKIP {t} {label}: no reference output", flush=True); continue
        expected = hashlib.sha256(oracle.read_bytes()).hexdigest()
        data = cache / label / t
        if not data.exists(): shutil.copytree(ROOT / ".data" / label / t, data)
        out = ROOT / ".results/candidates" / label / t
        out.mkdir(parents=True, exist_ok=True)
        # Output on Linux storage too: the scoring input is page-cache warm.
        native_out = cache / "outputs" / label / t
        native_out.mkdir(parents=True, exist_ok=True)
        for path in data.iterdir():
            with path.open("rb") as stream:
                while stream.read(1 << 20): pass
        for attempt in range(3 if label == "full-1" else 1):
            timing = out / "time.txt"
            subprocess.run(["/usr/bin/time", "-f", "%e %U %S %M", "-o", str(timing), str(binary), str(data), str(native_out)], check=True)
            actual = hashlib.sha256((native_out / filename).read_bytes()).hexdigest()
            assert actual == expected, (t, label, expected, actual)
            wall, user, system, rss = timing.read_text().split()
            row = dict(track=t, label=label, attempt=attempt+1, process_s=float(wall), user_s=float(user), system_s=float(system), rss_kib=int(rss), sha256=actual)
            rows.append(row); print(json.dumps(row), flush=True)
(ROOT / ".results/candidates" / ("-".join(args.tracks) + ".json")).write_text(json.dumps(rows, indent=2) + "\n")
