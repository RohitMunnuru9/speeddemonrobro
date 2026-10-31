"""Check the C optimization against already-generated reference outputs."""
import hashlib
import json
from pathlib import Path
import subprocess
from verify import ROOT

records = []
for label in ("tiny-1", "tiny-2", "sample-1", "sample-2", "full-1", "full-2"):
    ref = ROOT / ".results" / label / "c/reference/aggregates.txt"
    if not ref.exists():
        continue
    out = ROOT / ".results" / label / "c/optimized"
    out.mkdir(exist_ok=True)
    timer = out / "time.txt"
    subprocess.run(["/usr/bin/time", "-f", "%e %U %S %M", "-o", str(timer), str(ROOT / ".build/c-v2"), str(ROOT / ".data" / label / "c"), str(out)], check=True)
    expected = hashlib.sha256(ref.read_bytes()).hexdigest()
    actual = hashlib.sha256((out / "aggregates.txt").read_bytes()).hexdigest()
    assert actual == expected, label
    wall, user, system, rss = timer.read_text().split()
    row = dict(label=label, track="c", variant="global-names-mmap", sha256=actual, process_s=float(wall), user_s=float(user), system_s=float(system), peak_rss_kib=int(rss))
    records.append(row)
    print(json.dumps(row), flush=True)
(ROOT / ".results/c-optimized.json").write_text(json.dumps(records, indent=2) + "\n")
