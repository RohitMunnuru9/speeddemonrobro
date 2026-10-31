"""Validate the official downloaded B input and published output hash."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from verify import ROOT

expected = "f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927"
data = Path("/tmp/vayu-bench-inputs/full-public-1/b")
if not data.exists(): shutil.copytree(ROOT / ".data/full-public-1/b", data)
out = data.parent / "b-output"; out.mkdir(exist_ok=True)
binary = ROOT / ".build/b"
subprocess.run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-pthread", "-Wall", "-Wextra", "-Wpedantic", str(ROOT / "track-b/main.cpp"), "-o", str(binary)], check=True)
rows = []
for attempt in range(3):
    timer = out / "time.txt"
    subprocess.run(["/usr/bin/time", "-f", "%e %U %S %M", "-o", str(timer), str(binary), str(data), str(out)], check=True)
    actual = hashlib.sha256((out / "results.txt").read_bytes()).hexdigest()
    assert actual == expected, (expected, actual)
    wall, user, system, rss = timer.read_text().split()
    row = dict(track="b", attempt=attempt+1, process_s=float(wall), user_s=float(user), system_s=float(system), rss_kib=int(rss), sha256=actual)
    rows.append(row); print(json.dumps(row), flush=True)
(ROOT / ".results/b-full.json").write_text(json.dumps(rows, indent=2) + "\n")
