"""Force label-table growth, wide distances and generation wrap independently."""
import os
from pathlib import Path
import subprocess
import tempfile
from verify import ROOT

# 21-bit node IDs leave ten generation bits in packed labels. More than 1,023
# nontrivial queries on one worker must physically clear the old generations.
n, length = 1_740_512, 2_200
weights = [0 if i % 13 == 0 else 4_294_967_295 - (i % 5 == 0)
           for i in range(length)]
prefix = [0]
for weight in weights:
    prefix.append(prefix[-1] + weight)
queries = [(i % 3, length - i % 7) for i in range(2050)]
queries += [(10, 10), (length, 0), (n - 1, 0), (0, n - 1)]
expected = ''.join(f'{prefix[t]-prefix[s]}\n' if s <= t <= length else
                   ('0\n' if s == t else '-1\n') for s, t in queries)
with tempfile.TemporaryDirectory() as folder:
    data, out = Path(folder)/'data', Path(folder)/'out'
    data.mkdir(); out.mkdir()
    (data/'graph.txt').write_text(f'{n} {length}\n' + ''.join(
        f'{i} {i+1} {weight}\n' for i, weight in enumerate(weights)))
    (data/'queries.txt').write_text(''.join(f'{s} {t}\n' for s,t in queries))
    subprocess.run([str(ROOT/'.build/d'),str(data),str(out)],
                   env={**os.environ,'SD_THREADS':'1'},check=True)
    assert (out/'distances.txt').read_text() == expected
print(f'D: {len(queries)} exact answers; wide labels, rehash and generation wrap passed')
