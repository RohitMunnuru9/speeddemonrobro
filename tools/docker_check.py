"""Offline builds and sandbox/hash smoke checks, usable from Windows Python."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = {"a": "summary.txt", "b": "results.txt", "c": "aggregates.txt", "d": "distances.txt"}

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], cwd=ROOT, check=True, **kwargs)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="sample-1")
    parser.add_argument("--tracks", nargs="+", default=list(OUTPUT))
    parser.add_argument("--expected", help="explicit published hash, for a single selected track")
    args = parser.parse_args()
    if args.expected and len(args.tracks) != 1:
        parser.error("--expected requires exactly one track")
    evidence = []
    for track in args.tracks:
        name = OUTPUT[track]
        run(["python", ROOT / "organizer/harness/dockerfile_check.py", ROOT / f"track-{track}/Dockerfile", ROOT / "organizer/harness/allowlist.json"])
        tag = f"speeddemon-local/{track}"
        run(["docker", "build", "--network=none", "-t", tag, ROOT / f"track-{track}"], timeout=600)
        image_size = int(run(["docker", "image", "inspect", "-f", "{{.Size}}", tag], capture_output=True, text=True).stdout)
        assert image_size <= 2 * 1024**3
        data = ROOT / ".data" / args.label / track
        expected = args.expected or hashlib.sha256((ROOT / ".results" / args.label / track / "reference" / name).read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix="sd-", dir=ROOT / ".results") as folder:
            start = time.perf_counter()
            run(["docker", "run", "--rm", "--network=none", "--read-only", "--tmpfs", "/tmp:rw,size=1g,exec", "--memory=8g", "--memory-swap=8g", "--pids-limit=512", "--cap-drop=ALL", "--security-opt=no-new-privileges", "--user", "65534:65534", "--ulimit", "nofile=8192:8192", "-e", f"TRACK={track}", "-v", f"{data}:/data:ro", "-v", f"{folder}:/out:rw", tag], timeout=300)
            elapsed = time.perf_counter() - start
            assert sorted(p.name for p in Path(folder).iterdir()) == [name]
            actual = hashlib.sha256((Path(folder) / name).read_bytes()).hexdigest()
            assert actual == expected, (track, expected, actual)
            row = dict(track=track, dataset=args.label, image_bytes=image_size, sandbox_hash_pass=True, sha256=actual, container_wall_including_startup_s=round(elapsed, 4))
            print(json.dumps(row), flush=True)
            evidence.append(row)
    (ROOT / ".results/docker.json").write_text(json.dumps(evidence, indent=2) + "\n")

if __name__ == "__main__":
    main()
