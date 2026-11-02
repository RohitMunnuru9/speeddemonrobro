import sys,re,subprocess,os,tempfile,hashlib
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from verify import ROOT,load_test
import argparse
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=ROOT/'.build/a');args=p.parse_args();binary=args.binary.resolve()
official=load_test('a')
s=Path('tools/edge_a.py').read_text();s=re.sub(r'ROOT\s*/\s*"\.build/a"','binary',s);exec(compile(s,'tools/edge_a.py','exec'))
cases=[]
cases.append('2026-10-30T18:00:00.000000Z GET /front 200 3\n'*50000+'2026-10-30T20:00:00.000000Z GET /steady 599 4\n'*100000+'2026-10-30T18:00:00.000000Z GET /late 599 4294967295\n')
cases.append(''.join(f'2026-10-30T18:00:00.000000Z GET /region 200 {i%256}\n'for i in range(270000)))
names=['/'+'a'*15+f'{i:04}'+ 'a'*76 for i in range(400)]
cases.append(''.join(f'2026-10-30T18:00:00.000000Z GET {names[i%400]} {200 if i%3 else 599} {i%103}\n'for i in range(16000)))
for index,raw in enumerate(cases):
 expected=official.run_reference(raw).encode()
 with tempfile.TemporaryDirectory()as folder:
  data,out=Path(folder)/'data',Path(folder)/'out';data.mkdir();out.mkdir();(data/'requests.log').write_text(raw)
  for workers in [1,12]:
   subprocess.run([str(binary),str(data),str(out)],env={**os.environ,'SD_THREADS':str(workers)},check=True,timeout=30)
   assert(out/'summary.txt').read_bytes()==expected,(index,workers)
 print('stream edge',index,'PASS',flush=True)
with tempfile.TemporaryDirectory()as folder:
 data,out=Path(folder)/'data',Path(folder)/'out';data.mkdir();out.mkdir()
 (data/'requests.log').write_text(''.join(f'2026-10-30T18:00:00.000000Z GET /io/{i:04} 200 1\n'for i in range(500)))
 (out/'summary.txt').symlink_to('/dev/full')
 result=subprocess.run([str(binary),str(data),str(out)],env={**os.environ,'SD_THREADS':'12'},capture_output=True,timeout=30)
 assert result.returncode==1 and (b'output failed' in result.stderr or b'No space left' in result.stderr),result
print('early write failure PASS',flush=True)
for label in ['tiny-1','tiny-2','sample-1','sample-2','full-1','full-2']:
 data=ROOT/'.data'/label/'a';expected=(ROOT/'.results'/label/'a/reference/summary.txt').read_bytes()
 with tempfile.TemporaryDirectory()as out:
  subprocess.run([str(binary),str(data),out],env={**os.environ,'SD_THREADS':'12'},check=True,timeout=30)
  assert(Path(out)/'summary.txt').read_bytes()==expected,label
 print('public',label,'PASS',flush=True)
