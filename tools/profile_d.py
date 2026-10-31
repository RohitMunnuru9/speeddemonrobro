"""Compile a temporary instrumented copy; production source stays unchanged."""
import os
from pathlib import Path
import subprocess
from verify import ROOT

source = (ROOT / "track-d/main.cpp").read_text()
source = '#include <chrono>\n' + source
source = source.replace('    // Oversized contracted weights', '    std::cerr << "compression " << n << " -> " << compact_n << ", edges " << m << " -> " << compact.size() << ", overflow " << overflow << "\\n";\n    // Oversized contracted weights')
source = source.replace('    Mapping input(', '    auto start = std::chrono::steady_clock::now();\n    Mapping input(')
source = source.replace('    std::vector<std::pair<uint32_t, uint32_t>> queries;', '    auto built = std::chrono::steady_clock::now();\n    std::cerr << "build " << std::chrono::duration<double>(built-start).count() << " s\\n";\n    std::vector<std::pair<uint32_t, uint32_t>> queries;')
source = source.replace('    std::string output;', '    std::cerr << "queries " << std::chrono::duration<double>(std::chrono::steady_clock::now()-built).count() << " s\\n";\n    std::string output;')
temp = ROOT / ".build/profile-d.cpp"
temp.write_text(source)
binary = ROOT / ".build/profile-d"
subprocess.run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-pthread", "-I", str(ROOT / "track-d"), str(temp), "-o", str(binary)], check=True)
for workers in (1, 2, 4, 6):
    env = os.environ.copy(); env["SD_THREADS"] = str(workers)
    print(f"workers={workers}", flush=True)
    subprocess.run([str(binary), "/tmp/vayu-bench-inputs/full-1/d", "/tmp/vayu-bench-inputs/outputs/full-1/d"], env=env, check=True)
