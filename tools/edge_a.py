"""Exercise the log parser across record widths, dates and uint32 latencies."""
from datetime import datetime, timedelta
import os
from pathlib import Path
import random
import subprocess
import tempfile
from verify import ROOT, load_test

official = load_test("a")
rng = random.Random(4521)
values = [0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999,
          100000, 999999, 1000000, 9999999, 10000000, 99999999,
          100000000, 999999999, 1000000000, 4294967295]
lines = []
start = datetime(2026, 10, 30, 18)
for width in range(1, 130):
    for method in ("GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"):
        for latency in values:
            stamp = (start + timedelta(hours=rng.randrange(72))).strftime("%Y-%m-%dT%H:00:00.000000Z")
            lines.append(f"{stamp} {method} /{'a'*(width-1)} {rng.choice([100,200,499,500,599])} {latency}\n")
rng.shuffle(lines)
stress = "".join(lines)
cases = ["", official.WORKED_INPUT, official.edge_input(), stress]
for raw in cases:
    expected = official.run_reference(raw).encode()
    with tempfile.TemporaryDirectory() as folder:
        data, out = Path(folder)/"data", Path(folder)/"out"
        data.mkdir(); out.mkdir()
        (data/"requests.log").write_bytes(raw.encode())
        for workers in (1, 6, 12):
            subprocess.run([str(ROOT/".build/a"), str(data), str(out)],
                           env={**os.environ,"SD_THREADS":str(workers)},check=True)
            assert (out/"summary.txt").read_bytes() == expected, (len(raw), workers)
print(f"Track A: 12 byte-exact checks passed, including {len(lines)} shuffled stress records")
