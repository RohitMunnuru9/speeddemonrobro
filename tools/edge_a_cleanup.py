"""Check that an early output failure cannot strand cleanup-barrier workers."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from verify import ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, default=ROOT / '.build/a')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as folder:
    data, out = Path(folder) / 'data', Path(folder) / 'out'
    data.mkdir()
    out.mkdir()
    # Enough output in the first hour to fail write_all before later rendezvous
    # sends complete. /dev/full deterministically returns ENOSPC on Linux.
    (data / 'requests.log').write_text(''.join(
        f'2026-10-30T18:00:00.000000Z GET /cleanup/{i:04} 200 1\n'
        for i in range(500)))
    (out / 'summary.txt').symlink_to('/dev/full')
    result = subprocess.run([str(args.binary), str(data), str(out)],
                            env={**os.environ, 'SD_THREADS': '12'},
                            capture_output=True, timeout=30)
    assert result.returncode == 1, (result.returncode, result.stderr)
    assert b'No space left' in result.stderr, result.stderr
print('A: early write error terminates all twelve cleanup workers')
