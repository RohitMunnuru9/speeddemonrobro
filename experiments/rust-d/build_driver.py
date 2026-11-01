"""Generate the C++ graph frontend for the experimental Rust search engine."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'track-d/main.cpp').read_text()
start = source.index('using Item =')
end = source.index('struct alignas(16) Node', start)
source = source[:start] + source[end:]
start = source.index('int main(')
source = source[:start] + '''struct Query {uint32_t source,target;};
extern "C" void vayu_solve(uint32_t,const Node*,const Node*,const uint32_t*,const uint32_t*,const uint64_t*,const uint64_t*,uint32_t,const Query*,size_t,int64_t*,size_t);
''' + source[start:]
source = source.replace('std::vector<std::pair<uint32_t, uint32_t>> queries;',
                        'std::vector<Query> queries;')
start = source.index('    parallel(workers, [&](size_t) {')
end = source.index('    // PROFILE_SEARCH_DONE', start)
source = source[:start] + '''    vayu_solve(n,nodes[0].get(),nodes[1].get(),edges[0].get(),edges[1].get(),wide[0].get(),wide[1].get(),weight_bits,queries.data(),queries.size(),answers.data(),workers);
''' + source[end:]
Path(sys.argv[1]).write_text(source)
