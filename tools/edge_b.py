"""Target B's SIMD boundaries, exact phrase positions and reserve overflow."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from verify import ROOT, load_test

subprocess.run(['g++', '-O3', '-std=c++20', '-march=x86-64-v3', '-pthread', '-Wall', '-Wextra', '-Wpedantic', str(ROOT/'track-b/main.cpp'), '-o', str(ROOT/'.build/b')], check=True)
reference = load_test('b')
longword = 'a' * 32
texts = [' ' * offset + longword.upper() + ' alpha--ALPHA beta ' + longword + ' café 007 ✴ tail' for offset in range(64)]
texts += ['', '§§ —', 'alpha alpha alpha beta', 'z ' * 35000 + 'rareend alpha beta']
queries = [f'BOOL {longword}', 'BOOL alpha NOT beta OR 007 AND tail', 'BOOL 007 NOT 7', 'BOOL absent OR alpha NOT alpha']
queries += [f'PHRASE "{phrase}"' for phrase in [f'{longword} alpha alpha', f'beta {longword}', 'alpha alpha beta', 'rareend alpha beta', 'z rareend alpha', 'alpha beta 007', 'caf 007', 'alpha alpha alpha alpha']]
datasets = [('boundaries', reference.docs_file(texts), ('\n'.join(queries)+'\n').encode())]
# First-sample absence must never mean corpus-wide absence. The later block
# exceeds predicted posting capacity and forces exact migration to overflow.
docs = reference.docs_file(['lead alpha'] * 4096 + ['tail beta tail beta'] * 904)
query = b'BOOL tail\nBOOL lead OR tail\nBOOL tail NOT lead\nPHRASE "tail beta tail"\nPHRASE "beta tail beta"\n'
datasets.append(('reserve-overflow', docs, query))
for label, docs, queries in datasets:
    expected = reference.solve(docs, queries)
    with tempfile.TemporaryDirectory() as folder:
        data = Path(folder)/'data'; out = Path(folder)/'out'
        data.mkdir(); out.mkdir()
        (data/'docs.txt').write_bytes(docs); (data/'queries.txt').write_bytes(queries)
        for threads in (1, 6, 12):
            subprocess.run([str(ROOT/'.build/b'), str(data), str(out)], env={**os.environ, 'SD_THREADS':str(threads)}, check=True)
            actual = (out/'results.txt').read_bytes()
            assert actual == expected, (label, threads, hashlib.sha256(actual).hexdigest())
            print(label, threads, 'PASS', flush=True)
