"""Official hand-written edge cases and arbitrary-graph oracle comparisons."""
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
from verify import ROOT, load_test

count = 0
def check(track, files, expected, threads=None):
    global count
    with tempfile.TemporaryDirectory() as folder:
        data, out = Path(folder) / "data", Path(folder) / "out"
        data.mkdir(); out.mkdir()
        for name, value in files.items():
            (data / name).write_bytes(value.encode() if isinstance(value, str) else value)
        env = os.environ.copy()
        if threads: env["SD_THREADS"] = str(threads)
        binary = ROOT / ".build" / ("c-v2" if track == "c" else track)
        subprocess.run([str(binary), str(data), str(out)], env=env, check=True)
        result = next(out.iterdir()).read_bytes()
        assert result == (expected.encode() if isinstance(expected, str) else expected), track
        count += 1

subprocess.run(["g++", "-std=c++20", "-O3", "-pthread", "-Wall", "-Wextra", "-Wpedantic", str(ROOT / "track-c/main.cpp"), "-o", str(ROOT / ".build/c-v2")], check=True)
a, c, d = (load_test(t) for t in ("a", "c", "d"))
check("a", {"requests.log": ""}, "")
check("a", {"requests.log": a.edge_input()}, a.run_reference(a.edge_input()))
check("c", {"records.csv": c.WORKED_RECORDS, "regions.csv": c.WORKED_REGIONS}, c.WORKED_OUTPUT)
check("c", {"records.csv": c.RULES_RECORDS, "regions.csv": c.RULES_REGIONS}, c.RULES_OUTPUT)
for threads in (1, 6, 12):
    check("d", {"graph.txt": d.EXAMPLE_GRAPH, "queries.txt": d.EXAMPLE_QUERIES}, d.EXAMPLE_OUTPUT, threads)
# Bellman-Ford is an independent oracle and does not depend on the event's
# lattice structure or on either implementation's Dijkstra search.
for seed in range(10):
    rng = random.Random(seed)
    n = 30
    edges = [(rng.randrange(n), rng.randrange(n), rng.choice([0, 1, 4294967295, rng.randrange(100)])) for _ in range(80)]
    queries = [(s, t) for s in range(n) for t in range(n)]
    expected = []
    for s in range(n):
        dist = [float("inf")] * n; dist[s] = 0
        for _ in range(n - 1):
            changed = False
            for u, v, w in edges:
                if dist[u] + w < dist[v]:
                    dist[v] = dist[u] + w; changed = True
            if not changed: break
        expected.extend("-1" if value == float("inf") else str(value) for value in dist)
    graph = f"{n} {len(edges)}\n" + "".join(f"{u} {v} {w}\n" for u, v, w in edges)
    query = "".join(f"{s} {t}\n" for s, t in queries)
    check("d", {"graph.txt": graph, "queries.txt": query}, "\n".join(expected) + "\n", 6)
print(json.dumps(dict(cases=count, random_graph_query_answers=9000, passed=True)))
