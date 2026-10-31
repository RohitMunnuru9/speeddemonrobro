"""Compare full seed-17 build inputs with the independent official reference.

This is local validation only. Intermediate build images never enter submission.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODULES = {'a':'a_ingest', 'b':'b_index', 'c':'c_join', 'd':'d_traverse'}
OUTPUTS = {'a':'summary.txt', 'b':'results.txt', 'c':'aggregates.txt', 'd':'distances.txt'}
parser = argparse.ArgumentParser()
parser.add_argument('--tracks', nargs='+', choices=list(MODULES), default=list(MODULES))
args = parser.parse_args()
for track in args.tracks:
    tag = f'speeddemon-validation/{track}'
    subprocess.run(['docker', 'build', '--network=none', '--target', 'build', '-t', tag, str(ROOT/f'track-{track}')], check=True, timeout=600)
    code = f'''import hashlib,json,pathlib,subprocess
subprocess.run(['python3','/reference/{MODULES[track]}.py','--data','/training','--out','/out/reference'],check=True)
subprocess.run(['/solution','/training','/out/ours'],check=True)
expected=pathlib.Path('/out/reference/{OUTPUTS[track]}').read_bytes()
actual=pathlib.Path('/out/ours/{OUTPUTS[track]}').read_bytes()
assert expected==actual, 'independent seed-17 mismatch'
print(json.dumps(dict(track={track!r},scale='full',seed=17,reference_match=True,sha256=hashlib.sha256(actual).hexdigest())))
'''
    subprocess.run(['docker', 'run', '--rm', '--network=none', '--read-only', '--memory=8g', '--memory-swap=8g', '--user', '65534:65534', '--tmpfs', '/out:rw,size=256m,mode=1777', '--mount', f'type=bind,src={ROOT / "organizer/reference"},dst=/reference,readonly', '-e', 'SD_THREADS=12', '--entrypoint', 'python3', tag, '-c', code], check=True, timeout=400)
