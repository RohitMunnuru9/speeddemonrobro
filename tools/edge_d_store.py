"""Force packed temporary rows, wide escapes and bucket migration together."""
import os
from pathlib import Path
import subprocess
import tempfile
from verify import ROOT

# Large enough to use all six parser workers; a concentrated star overflows
# their estimated coarse buckets. Both normal and escaped rows must migrate.
n, leaves = 1_740_512, 40_000
weights = [0, 1, 32767, 32768, 65535, 4_294_967_295]
forward = {v: weights[v % len(weights)] for v in range(1, leaves + 1)}
reverse = {v: weights[(v * 5 + 1) % len(weights)] for v in forward}
rows = [(0, v, forward[v]) for v in forward]
rows += [(v, 0, reverse[v]) for v in forward]
rows += [(0, 0, 0), (0, 1, 4_294_967_295)]
queries = [(0, 0), (0, n - 1), (n - 1, 0), (n - 1, n - 1)]
queries += [(0, v) for v in range(1, 13)]
queries += [(v, 0) for v in range(1, 13)]
queries += [(v, leaves - v) for v in range(1, 65)]

def distance(s, t):
    if s == t:
        return 0
    if s > leaves or t > leaves:
        return -1
    return (reverse[s] if s else 0) + (forward[t] if t else 0)

expected = ''.join(f'{distance(s, t)}\n' for s, t in queries)
with tempfile.TemporaryDirectory() as folder:
    data, out = Path(folder) / 'data', Path(folder) / 'out'
    data.mkdir()
    out.mkdir()
    (data / 'graph.txt').write_text(f'{n} {len(rows)}\n' + ''.join(
        f'{u} {v} {w}\n' for u, v, w in rows))
    assert (data / 'graph.txt').stat().st_size > 1_000_000
    (data / 'queries.txt').write_text(''.join(f'{s} {t}\n' for s, t in queries))
    for workers in (1, 12):
        subprocess.run([str(ROOT / '.build/d'), str(data), str(out)],
                       env={**os.environ, 'SD_THREADS': str(workers)}, check=True)
        assert (out / 'distances.txt').read_text() == expected
print(f'D: {len(queries) * 2} packed/wide/migration star answers passed')
