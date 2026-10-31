"""Compare allowlisted GCC and Swift-image Clang on identical C++ sources."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IMAGES = {'gcc': ('gcc:15@sha256:eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce','g++'), 'clang': ('swift:6.2@sha256:d6f8f6112e9a63da0a4a151d6b93acd16699d35c0b82531ac1a245f5af210197','clang++')}
EXPECTED = {'a':('summary.txt','495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3'), 'b':('results.txt','f86973c210228141bcb6f1f1b10188ad2a9b65f71e7cd7cf4590b38de4e80927'), 'c':('aggregates.txt','d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536'), 'd':('distances.txt','a23dbdf0a3e49cc8901310a9ee1dc6a9a569a85297b6da75c2a7002cb2ed277e')}
parser=argparse.ArgumentParser();parser.add_argument('--tracks',nargs='+',choices=['b','c','d'],default=['c','d']);args=parser.parse_args()
for track in args.tracks:
    if not (ROOT/f'track-{track}/main.cpp').exists():
        parser.error(f'Track {track} is no longer a C++ entry')
    for name,(image,compiler) in IMAGES.items():
        subprocess.run(['docker','run','--rm','--network=none','--mount',f'type=bind,src={ROOT/f"track-{track}"},dst=/src,readonly','--mount','type=volume,src=vayu-benchmark-data,dst=/bench','--entrypoint',compiler,image,'-std=c++20','-O3','-march=x86-64-v3','-pthread','-static-libstdc++','-static-libgcc','/src/main.cpp','-o',f'/bench/{name}-{track}'],check=True,timeout=120)
rows=[]
for track in args.tracks:
    filename,expected=EXPECTED[track]
    for compiler in IMAGES:
        command=f'TIMEFORMAT=%R; for i in 1 2 3; do time /bench/{compiler}-{track} /bench/{track} /out; done; sha256sum /out/{filename}; cat /sys/fs/cgroup/memory.peak'
        result=subprocess.run(['docker','run','--rm','--network=none','--read-only','--cpuset-cpus','0-3','--memory=8g','--memory-swap=8g','--user','65534:65534','--tmpfs','/out:rw,size=64m,mode=1777','--mount','type=volume,src=vayu-benchmark-data,dst=/bench,readonly','-e','SD_THREADS=12','--entrypoint','/bin/bash','debian:13-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad','-c',command],check=True,capture_output=True,text=True)
        times=[float(line) for line in result.stderr.splitlines() if line.strip()]
        lines=result.stdout.splitlines();assert lines[0].split()[0]==expected,(track,compiler,result.stdout)
        row=dict(track=track,compiler=compiler,times_s=times,median_s=statistics.median(times),local_cgroup_peak_bytes=int(lines[-1]))
        print(json.dumps(row),flush=True);rows.append(row)
(ROOT/'.results/compilers.json').write_text(json.dumps(rows,indent=2)+'\n')
