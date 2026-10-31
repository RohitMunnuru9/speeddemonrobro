"""Compile a temporary phase-instrumented copy without changing production code."""
import argparse
import subprocess
from verify import ROOT

parser = argparse.ArgumentParser()
parser.add_argument("track", choices=["a", "b", "d"])
args = parser.parse_args()
phases = {
    "a": [("PARSE_DONE", "parse"), ("MERGE_DONE", "merge"), ("SCATTER_DONE", "scatter"), ("FORMAT_DONE", "format")],
    "b": [("PREPARED_QUERIES", "query-prepare"), ("INDEX_DONE", "index"), ("READY_TO_QUERY", "query-index"), ("QUERIES_DONE", "queries")],
    "d": [("GRAPH_DONE", "graph"), ("SEARCH_DONE", "queries")],
}
source = (ROOT / f"track-{args.track}/main.cpp").read_text()
source = '#include <chrono>\n' + source
source = source.replace('  try {', '  try {\n    auto tick=std::chrono::steady_clock::now(); auto phase=[&](const char *name) { auto now=std::chrono::steady_clock::now(); std::cerr << name << " " << std::chrono::duration<double>(now-tick).count() << " s\\n"; tick=now; };', 1)
for marker, name in phases[args.track]:
    needle = f"// PROFILE_{marker}"
    assert needle in source, needle
    source = source.replace(needle, f'phase("{name}");')
temp = ROOT / f".build/profile-{args.track}.cpp"; temp.write_text(source)
binary = ROOT / f".build/profile-{args.track}"
subprocess.run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-pthread", "-I", str(ROOT / f"track-{args.track}"), str(temp), "-o", str(binary)], check=True)
data = f"/tmp/vayu-bench-inputs/{'full-public-1' if args.track == 'b' else 'full-1'}/{args.track}"
out = f"/tmp/vayu-bench-inputs/{'full-public-1/b-output' if args.track == 'b' else 'outputs/full-1/' + args.track}"
subprocess.run([str(binary), data, out], check=True)
