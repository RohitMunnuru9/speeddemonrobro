"""Temporary instrumentation for B indexing and query phases."""
import subprocess
from verify import ROOT
s = '#include <chrono>\n' + (ROOT / "track-b/main.cpp").read_text()
s = s.replace('    Mapping corpus(', '    auto last = std::chrono::steady_clock::now(); auto phase = [&](const char *name) { auto now=std::chrono::steady_clock::now(); std::cerr << name << " " << std::chrono::duration<double>(now-last).count() << "\\n"; last=now; };\n    Mapping corpus(')
s = s.replace('    const size_t documents', '    phase("queries_parse"); const size_t documents')
s = s.replace('    std::vector<IDs> postings', '    phase("index"); std::vector<IDs> postings')
s = s.replace('    std::vector<std::vector<IDs>>().swap(local);', '    std::vector<std::vector<IDs>>().swap(local); phase("merge");')
s = s.replace('    std::vector<std::vector<IDs>>().swap(local);', '    size_t bitmap_bytes=0, posting_count=0; for (auto &x:bitmaps) bitmap_bytes+=x.size()*8; for (auto &x:postings) posting_count+=x.size(); std::cerr << "terms " << words.size() << " bitmap_bytes " << bitmap_bytes << " posting_ids " << posting_count << "\\n";\n    std::vector<std::vector<IDs>>().swap(local);')
s = s.replace('    std::string joined;', '    phase("queries"); std::string joined;')
path=ROOT / ".build/profile-b.cpp"; path.write_text(s)
binary=ROOT / ".build/profile-b"
subprocess.run(["g++", "-std=c++20", "-O3", "-march=x86-64-v3", "-pthread", "-I", str(ROOT / "track-b"), str(path), "-o", str(binary)], check=True)
subprocess.run([str(binary), "/tmp/vayu-bench-inputs/full-public-1/b", "/tmp/vayu-bench-inputs/full-public-1/b-output"], check=True)
