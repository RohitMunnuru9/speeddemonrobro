#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct Input {
  std::string bytes; size_t p = 0;
  explicit Input(const std::string &path) {
    std::ifstream f(path, std::ios::binary); if (!f) throw std::runtime_error("cannot open " + path);
    bytes.assign(std::istreambuf_iterator<char>(f), {});
  }
  uint32_t number() { while (p < bytes.size() && bytes[p] <= ' ') ++p; uint32_t n = 0; while (p < bytes.size() && bytes[p] >= '0' && bytes[p] <= '9') n = n * 10 + bytes[p++] - '0'; return n; }
  bool more() { while (p < bytes.size() && bytes[p] <= ' ') ++p; return p < bytes.size(); }
};
struct Edge { uint32_t v, w; };
struct Raw { uint32_t u, v, w; };
int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "/data", out = argc > 2 ? argv[2] : "/out";
    Input f(data + "/graph.txt");
    uint32_t n = f.number(), m = f.number();
    std::vector<uint32_t> offsets(n + 1), indegree(n);
    std::vector<Raw> raw; raw.reserve(m);
    for (uint32_t i = 0; i < m; ++i) {
      uint32_t u = f.number(), v = f.number(), w = f.number();
      raw.push_back({u, v, w}); ++offsets[u + 1]; ++indegree[v];
    }
    for (uint32_t i = 1; i <= n; ++i) offsets[i] += offsets[i - 1];
    std::vector<Edge> edges(m);
    auto cursor = offsets;
    for (auto e : raw) edges[cursor[e.u]++] = {e.v, e.w};
    // Release build buffers before creating per-worker search state.
    std::vector<Raw>().swap(raw); std::vector<uint32_t>().swap(cursor); std::string().swap(f.bytes);
    Input q(data + "/queries.txt");
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    while (q.more()) { auto s = q.number(), t = q.number(); queries.emplace_back(s, t); }
    std::vector<int64_t> answers(queries.size(), -1);
    std::atomic<size_t> next{0};
    size_t workers = std::min<size_t>(6, std::max<size_t>(1, queries.size()));
    if (const char *env = std::getenv("SD_THREADS")) workers = std::clamp(std::atoi(env), 1, 12);
    auto work = [&]() {
      std::vector<uint64_t> dist(n);
      std::vector<uint32_t> seen(n, 0); uint32_t epoch = 0;
      using Item = std::pair<uint64_t, uint32_t>;
      for (;;) {
        size_t i = next.fetch_add(1); if (i >= queries.size()) break;
        auto [s, t] = queries[i];
        if (s == t) { answers[i] = 0; continue; }
        if (!indegree[t] || offsets[s] == offsets[s + 1]) continue;
        ++epoch;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        seen[s] = epoch; dist[s] = 0; heap.emplace(0, s);
        while (!heap.empty()) {
          auto [d, u] = heap.top(); heap.pop();
          if (d != dist[u]) continue;
          if (u == t) { answers[i] = d; break; }
          for (uint32_t e = offsets[u]; e < offsets[u + 1]; ++e) {
            auto [v, w] = edges[e]; uint64_t nd = d + w;
            if (seen[v] != epoch || nd < dist[v]) {
              seen[v] = epoch; dist[v] = nd; heap.emplace(nd, v);
            }
          }
        }
      }
    };
    std::vector<std::thread> pool;
    for (size_t i = 0; i < workers; ++i) pool.emplace_back(work);
    for (auto &thread : pool) thread.join();
    std::filesystem::create_directories(out);
    std::ofstream result(out + "/distances.txt", std::ios::binary);
    if (!result) throw std::runtime_error("cannot open output");
    for (auto answer : answers) result << answer << '\n';
    if (!result) throw std::runtime_error("output write failed");
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
