"""Copy byte-identical official generators for permitted offline PGO training."""
import hashlib
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
names = {"a": "a_ingest", "b": "b_index", "c": "c_join", "d": "d_traverse"}
for track, name in names.items():
    blob = subprocess.check_output(["git", "-C", str(root / "organizer"), "show", f"HEAD:generators/{name}.py"])
    target = root / f"track-{track}/training_generator.py"
    target.write_bytes(blob)
    print(track, hashlib.sha256(blob).hexdigest())
