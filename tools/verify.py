"""Compile and compare our Linux binaries with the official reference.

Run in WSL or Linux: python3 tools/verify.py --scales tiny sample full --seeds 1 2
These are local wall/RSS measurements, not organizer scores or cgroup peaks.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
TRACKS = {"a": ("a_ingest", "summary.txt"), "b": ("b_index", "results.txt"), "c": ("c_join", "aggregates.txt"), "d": ("d_traverse", "distances.txt")}

def run(args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def compile_entry(track, binary):
    if track == "a":
        run(["docker", "run", "--rm", "--network=none", "-v", f"{ROOT / 'track-a'}:/src:ro",
             "-v", f"{binary.parent}:/output", "-w", "/src", "-e", "CARGO_TARGET_DIR=/tmp/target",
             "-e", "RUSTFLAGS=-C target-cpu=native",
             "rust:1-slim-trixie@sha256:1a939748a7cbab172de91a014e3804a97af1394a8c844faa67fce9401c4e40fb",
             "sh", "-c", f"cargo build --release --locked --offline && cp /tmp/target/release/vayu-log /output/{binary.name}"])
    else:
        run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-DNDEBUG", "-pthread",
             "-Wall", "-Wextra", "-Wpedantic", ROOT / f"track-{track}/main.cpp", "-o", binary])

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def compare(track, data, label, records):
    module, filename = TRACKS[track]
    folder = ROOT / ".results" / label / track
    ref, ours = folder / "reference", folder / "ours"
    ref.mkdir(parents=True, exist_ok=True)
    ours.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    run(["python3", ROOT / "organizer/reference" / (module + ".py"), "--data", data, "--out", ref])
    reference_s = time.perf_counter() - start
    timer = folder / "time.txt"
    start = time.perf_counter()
    run(["/usr/bin/time", "-f", "%e %M", "-o", timer, ROOT / ".build" / track, data, ours])
    elapsed = time.perf_counter() - start
    expected, actual = digest(ref / filename), digest(ours / filename)
    if expected != actual:
        raise AssertionError(f"{label}/{track}: HASH MISMATCH {expected} != {actual}")
    wall, rss = timer.read_text().split()
    record = dict(label=label, track=track, sha256=actual, reference_s=round(reference_s, 4), solution_s=round(elapsed, 4), process_s=float(wall), peak_rss_kib=int(rss))
    records.append(record)
    (ROOT / ".results/local.json").write_text(json.dumps(records, indent=2) + "\n")
    print(json.dumps(record), flush=True)

def load_test(track):
    module = TRACKS[track][0]
    spec = importlib.util.spec_from_file_location("cases_" + track, ROOT / "organizer/tests" / ("test_" + module + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scales", nargs="+", default=["tiny", "sample"])
    parser.add_argument("--seeds", nargs="+", type=int, default=[1, 2, 17])
    parser.add_argument("--tracks", nargs="+", default=list(TRACKS))
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    records = []
    for track in args.tracks:
        compile_entry(track, ROOT / ".build" / track)
        mod = load_test(track)
        data = ROOT / ".data/edge" / track
        data.mkdir(parents=True, exist_ok=True)
        if track == "a":
            (data / "requests.log").write_bytes(mod.WORKED_INPUT.encode())
        elif track == "b":
            (data / "docs.txt").write_bytes(mod.EXAMPLE_DOCS)
            (data / "queries.txt").write_bytes(mod.EXAMPLE_QUERIES)
        elif track == "c":
            (data / "records.csv").write_bytes(mod.WORKED_RECORDS)
            (data / "regions.csv").write_bytes(mod.WORKED_REGIONS)
        else:
            (data / "graph.txt").write_bytes(mod.EXAMPLE_GRAPH.encode())
            (data / "queries.txt").write_bytes(mod.EXAMPLE_QUERIES.encode())
        compare(track, data, "worked-example", records)
    for scale in args.scales:
        for seed in args.seeds:
            for track in args.tracks:
                module = TRACKS[track][0]
                data = ROOT / ".data" / f"{scale}-{seed}" / track
                print(f"Generating {track} {scale} seed {seed}", flush=True)
                run(["python3", ROOT / "organizer/generators" / (module + ".py"), "--seed", seed, "--scale", scale, "--out", data])
                compare(track, data, f"{scale}-{seed}", records)

if __name__ == "__main__":
    main()
