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
# Cross-thread duplicates exercise the shared exact membership table and mixed
# LF/CRLF chunk boundaries, with quote escapes and both empty-name encodings.
records = b'id,name,note,region_id,amount\r\n' + b''.join(
    str(i + 1).encode() + b',' + [b'"Ann ""A"""', b'""', b''][i % 3] + b',"a\nb",7,-0.01' + (b'\r\n' if i % 2 else b'\n')
    for i in range(60000))
regions = b'region_id,country\n7,AAA\n'
check("c", {"records.csv": records, "regions.csv": regions}, c.run_reference(records, regions))
# Equal first/last windows and lengths force more than 128 identical hashes.
# Full byte comparisons and the exact overflow map must still distinguish keys.
records = b'id,name,note,region_id,amount\r\n' + b''.join(
    f'{i+1},"abcdefgh{i%200:010d}mnopqrst","",{7+(i//200)%2},0.01\r\n'.encode()
    for i in range(20000))
regions = b'region_id,country\n7,AAA\n8,BBB\n'
check("c", {"records.csv": records, "regions.csv": regions}, c.run_reference(records, regions))
for threads in (1, 6, 12):
    check("d", {"graph.txt": d.EXAMPLE_GRAPH, "queries.txt": d.EXAMPLE_QUERIES}, d.EXAMPLE_OUTPUT, threads)
chain = [(u, u+1, 1) for u in range(199)] + [(u+1, u, 2) for u in range(199)] + [(200, 201, 4294967295), (201, 200, 4294967295)]
graph = f'202 {len(chain)}\n' + ''.join(f'{u} {v} {w}\n' for u,v,w in chain)
query = '0 199\n199 0\n50 150\n150 50\n50 50\n200 201\n0 201\n'
check("d", {"graph.txt": graph, "queries.txt": query}, '199\n398\n100\n200\n0\n4294967295\n-1\n', 6)
heavy = [(u, u+1, 4294967295) for u in range(3)] + [(u+1,u,4294967295) for u in range(3)]
check("d", {"graph.txt": '4 6\n'+''.join(f'{u} {v} {w}\n' for u,v,w in heavy), "queries.txt": '0 3\n3 0\n'}, '12884901885\n12884901885\n', 1)
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
