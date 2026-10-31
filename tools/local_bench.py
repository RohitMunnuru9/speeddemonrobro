"""Benchmark on the WSL Linux filesystem, avoiding OneDrive mount overhead.

Warm inputs first, time only exec-to-exit with GNU time. RSS is not cgroup M.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from verify import ROOT, TRACKS

rows = []
for track, (_, name) in TRACKS.items():
    ref = ROOT / ".results/full-1" / track / "reference" / name
    if not ref.exists(): continue
    with tempfile.TemporaryDirectory(prefix="sd-bench-") as folder:
        base = Path(folder)
        shutil.copytree(ROOT / ".data/full-1" / track, base / "data")
        (base / "out").mkdir()
        for path in (base / "data").iterdir():
            with path.open("rb") as stream:
                while stream.read(1 << 20): pass
        binary = ROOT / ".build" / ("c-v2" if track == "c" else track)
        for attempt in range(3):
            subprocess.run(["/usr/bin/time", "-f", "%e %U %S %M", "-o", str(base / "time"), str(binary), str(base / "data"), str(base / "out")], check=True)
            actual = hashlib.sha256((base / "out" / name).read_bytes()).hexdigest()
            assert actual == hashlib.sha256(ref.read_bytes()).hexdigest()
            wall, user, system, rss = (base / "time").read_text().split()
            row = dict(track=track, attempt=attempt+1, dataset="full-1", process_s=float(wall), user_s=float(user), system_s=float(system), peak_rss_kib=int(rss), sha256=actual)
            rows.append(row)
            print(json.dumps(row), flush=True)
(ROOT / ".results/linux-benchmark.json").write_text(json.dumps(rows, indent=2) + "\n")
