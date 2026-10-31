"""Local process-only timing on a Docker volume; never an official score."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
from verify import ROOT

parser = argparse.ArgumentParser()
parser.add_argument("--tracks", nargs="+", choices=["b", "c", "d"], default=["b", "c"])
parser.add_argument("--threads", nargs="+", type=int, default=[6, 12])
args = parser.parse_args()
expected = {"b": ("results.txt", "f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927"), "c": ("aggregates.txt", "d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536"), "d": ("distances.txt", "a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e")}
cpu_count = int(subprocess.check_output(["docker", "info", "--format", "{{.NCPU}}"], text=True))
cpuset = f"0-{min(12, cpu_count)-1}"
rows = []
for track in args.tracks:
    name, digest = expected[track]
    for threads in args.threads:
        if not 1 <= threads <= 12: parser.error("threads must be 1..12")
        command = f"TIMEFORMAT=%R; for i in 1 2 3; do time /app/solution /bench/{track} /out; done; sha256sum /out/{name}; cat /sys/fs/cgroup/memory.peak"
        result = subprocess.run(["docker", "run", "--rm", "--network=none", "--read-only", "--cpuset-cpus", cpuset, "--memory=8g", "--memory-swap=8g", "--pids-limit=512", "--user", "65534:65534", "--cap-drop=ALL", "--security-opt=no-new-privileges", "--tmpfs", "/out:rw,size=64m,mode=1777", "--mount", "type=volume,src=vayu-benchmark-data,dst=/bench,readonly", "-e", f"SD_THREADS={threads}", "--entrypoint", "/bin/bash", f"speeddemon-local/{track}", "-c", command], check=True, capture_output=True, text=True)
        times = [float(line) for line in result.stderr.splitlines() if line.strip()]
        output = result.stdout.splitlines()
        assert output[0].split()[0] == digest, (track, output)
        row = dict(track=track, threads=threads, cpuset=cpuset, times_s=times, median_s=statistics.median(times), local_cgroup_peak_bytes=int(output[-1]))
        rows.append(row); print(json.dumps(row), flush=True)
(ROOT / ".results/docker-bench.json").write_text(json.dumps(rows, indent=2) + "\n")
