"""Instrument a temporary A copy to find serial bottlenecks."""
import subprocess
from verify import ROOT

s = (ROOT / "track-a/main.cpp").read_text()
s = '#include <chrono>\n' + s
s = s.replace('    Mapping input(', '    auto last_time = std::chrono::steady_clock::now();\n    auto phase = [&](const char *name) { auto now = std::chrono::steady_clock::now(); std::cerr << name << " " << std::chrono::duration<double>(now-last_time).count() << "\\n"; last_time=now; };\n    Mapping input(')
s = s.replace('    parallel(workers, scan);', '    parallel(workers, scan); phase("parse");')
s = s.replace('    offsets[groups] = total; values.resize(total);', '    offsets[groups] = total; values.resize(total); phase("merge");')
s = s.replace('    std::vector<Worker>().swap(local);', '    std::vector<Worker>().swap(local); phase("scatter");')
s = s.replace('    std::filesystem::create_directories(out);', '    phase("format"); std::filesystem::create_directories(out);')
path = ROOT / ".build/profile-a.cpp"; path.write_text(s)
binary = ROOT / ".build/profile-a"
subprocess.run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-pthread", "-I", str(ROOT / "track-a"), str(path), "-o", str(binary)], check=True)
for _ in range(3): subprocess.run([str(binary), "/tmp/vayu-bench-inputs/full-1/a", "/tmp/vayu-bench-inputs/outputs/full-1/a"], check=True)
