#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <limits>
#include <utility>

using Item = std::pair<uint64_t, uint32_t>;
struct RadixHeap {
  std::array<std::vector<Item>, 65> buckets;
  uint64_t last = 0; size_t count = 0;
  static unsigned bucket(uint64_t difference) { return difference ? 64 - __builtin_clzll(difference) : 0; }
  void push(uint64_t key, uint32_t node) { buckets[bucket(last ^ key)].emplace_back(key, node); ++count; }
  uint64_t minimum() {
    if (buckets[0].empty()) {
      unsigned b = 1; while (buckets[b].empty()) ++b;
      last = UINT64_MAX;
      for (auto item : buckets[b]) last = std::min(last, item.first);
      for (auto item : buckets[b]) buckets[bucket(last ^ item.first)].push_back(item);
      buckets[b].clear();
    }
    return last;
  }
  Item pop() { minimum(); Item item = buckets[0].back(); buckets[0].pop_back(); --count; return item; }
  void clear() { for (auto &bucket : buckets) bucket.clear(); count = 0; last = 0; }
};
struct Edge { uint32_t v, w; };
int main(int argc, char **argv) {
  try {
    const std::string data = argc > 1 ? argv[1] : "/data", out = argc > 2 ? argv[2] : "/out";
    Mapping input(data + "/graph.txt"), query_file(data + "/queries.txt");
    const char *p = input.data;
    uint32_t n = number(p); ++p; uint32_t m = number(p); ++p;
    const char *first_edge = p;
    std::array<std::vector<uint32_t>, 2> offsets{std::vector<uint32_t>(n + 1), std::vector<uint32_t>(n + 1)};
    for (uint32_t i = 0; i < m; ++i) {
      uint32_t u = number(p); ++p; uint32_t v = number(p); ++p; number(p); ++p;
      ++offsets[0][u + 1]; ++offsets[1][v + 1];
    }
    for (auto &o : offsets) for (uint32_t i = 1; i <= n; ++i) o[i] += o[i - 1];
    std::array<std::vector<Edge>, 2> edges{std::vector<Edge>(m), std::vector<Edge>(m)};
    auto cursor = offsets; p = first_edge;
    for (uint32_t i = 0; i < m; ++i) {
      uint32_t u = number(p); ++p; uint32_t v = number(p); ++p; uint32_t w = number(p); ++p;
      edges[0][cursor[0][u]++] = {v, w}; edges[1][cursor[1][v]++] = {u, w};
    }
    for (auto &c : cursor) std::vector<uint32_t>().swap(c);
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    p = query_file.data;
    while (p < query_file.data + query_file.size) {
      uint32_t s = number(p); ++p; uint32_t t = number(p); ++p; queries.emplace_back(s, t);
    }
    std::vector<int64_t> answers(queries.size(), -1);
    std::atomic<size_t> next{0}; size_t workers = input.size > 1000000 ? 4 : 1;
    if (const char *env = std::getenv("SD_THREADS")) workers = std::clamp(std::atoi(env), 1, 12);
    parallel(workers, [&](size_t) {
      // Distances are read only when the generation tag matches. This avoids
      // initializing the larger arrays or clearing every node before each query.
      std::array<std::unique_ptr<uint64_t[]>, 2> dist{std::make_unique_for_overwrite<uint64_t[]>(n), std::make_unique_for_overwrite<uint64_t[]>(n)};
      std::array<std::vector<uint32_t>, 2> seen{std::vector<uint32_t>(n), std::vector<uint32_t>(n)};
      std::array<RadixHeap, 2> heap; uint32_t epoch = 0;
      for (;;) {
        size_t i = next.fetch_add(1); if (i >= queries.size()) break;
        auto [s, t] = queries[i];
        if (s == t) { answers[i] = 0; continue; }
        if (offsets[0][s] == offsets[0][s + 1] || offsets[1][t] == offsets[1][t + 1]) continue;
        ++epoch; heap[0].clear(); heap[1].clear();
        dist[0][s] = 0; dist[1][t] = 0; seen[0][s] = epoch; seen[1][t] = epoch;
        heap[0].push(0, s); heap[1].push(0, t);
        uint64_t best = UINT64_MAX;
        // Every meeting candidate is a real directed s->t path. Stop when the
        // two unsettled minima cannot improve the best such path.
        while (heap[0].count && heap[1].count) {
          uint64_t a = heap[0].minimum(), b = heap[1].minimum();
          if (best != UINT64_MAX && a + b >= best) break;
          unsigned side = heap[0].count <= heap[1].count ? 0 : 1, other = side ^ 1;
          auto [d, u] = heap[side].pop();
          if (d != dist[side][u]) continue;
          if (seen[other][u] == epoch) best = std::min(best, d + dist[other][u]);
          for (uint32_t e = offsets[side][u]; e < offsets[side][u + 1]; ++e) {
            auto [v, w] = edges[side][e]; uint64_t nd = d + w;
            if (seen[side][v] != epoch || nd < dist[side][v]) {
              seen[side][v] = epoch; dist[side][v] = nd; heap[side].push(nd, v);
              if (seen[other][v] == epoch) best = std::min(best, nd + dist[other][v]);
            }
          }
        }
        if (best != UINT64_MAX) answers[i] = best;
      }
    });
    std::string output;
    for (auto d : answers) { if (d < 0) output += "-1"; else append(output, d); output += '\n'; }
    std::filesystem::create_directories(out); write_file(out + "/distances.txt", output);
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
