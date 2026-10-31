#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

using Item = std::pair<uint64_t, uint32_t>;
struct RadixHeap {
  std::array<std::vector<Item>, 65> buckets;
  uint64_t last = 0;
  size_t count = 0;
  static unsigned bucket(uint64_t difference) {
    return difference ? 64 - __builtin_clzll(difference) : 0;
  }
  void push(uint64_t key, uint32_t node) {
    buckets[bucket(last ^ key)].emplace_back(key, node);
    ++count;
  }
  uint64_t minimum() {
    if (buckets[0].empty()) {
      unsigned b = 1;
      while (buckets[b].empty())
        ++b;
      last = UINT64_MAX;
      for (auto item : buckets[b])
        last = std::min(last, item.first);
      for (auto item : buckets[b])
        buckets[bucket(last ^ item.first)].push_back(item);
      buckets[b].clear();
    }
    return last;
  }
  Item pop() {
    minimum();
    Item item = buckets[0].back();
    buckets[0].pop_back();
    --count;
    return item;
  }
  void clear() {
    for (auto &bucket : buckets)
      bucket.clear();
    count = 0;
    last = 0;
  }
};
// The state grows with nodes visited by a query, rather than every graph node.
struct SearchState {
  struct Entry {
    uint64_t distance = UINT64_MAX;
    uint32_t node = 0, epoch = 0;
  };
  std::vector<Entry> entries = std::vector<Entry>(1024);
  uint32_t epoch = 0;
  size_t used = 0;
  static size_t hash(uint32_t node) {
    uint64_t h = node * 0x9e3779b97f4a7c15ULL;
    return h ^ (h >> 32);
  }
  void clear() {
    used = 0;
    if (++epoch == 0) {
      for (auto &entry : entries)
        entry.epoch = 0;
      epoch = 1;
    }
  }
  uint64_t get(uint32_t node) const {
    size_t slot = hash(node) & (entries.size() - 1);
    while (entries[slot].epoch == epoch) {
      if (entries[slot].node == node)
        return entries[slot].distance;
      slot = (slot + 1) & (entries.size() - 1);
    }
    return UINT64_MAX;
  }
  bool relax(uint32_t node, uint64_t distance) {
    if ((used + 1) * 4 > entries.size() * 3) {
      auto old = std::move(entries);
      entries.resize(old.size() * 2);
      used = 0;
      for (const auto &entry : old)
        if (entry.epoch == epoch)
          relax(entry.node, entry.distance);
    }
    size_t slot = hash(node) & (entries.size() - 1);
    while (entries[slot].epoch == epoch && entries[slot].node != node)
      slot = (slot + 1) & (entries.size() - 1);
    auto &entry = entries[slot];
    if (entry.epoch != epoch) {
      entry = {distance, node, epoch};
      ++used;
      return true;
    }
    if (distance >= entry.distance)
      return false;
    entry.distance = distance;
    return true;
  }
};
struct Edge {
  uint32_t v, w;
};
struct Raw {
  uint32_t u, v, w;
};
void radix_sort(std::vector<Raw> &rows, uint32_t nodes, bool reverse,
                size_t workers) {
  std::vector<Raw> scratch(rows.size());
  std::vector<std::array<size_t, 2048>> counts(workers), positions(workers);
  for (unsigned shift = 0; (uint64_t(nodes - 1) >> shift) != 0; shift += 11) {
    parallel(workers, [&](size_t t) {
      counts[t].fill(0);
      for (size_t i = rows.size() * t / workers;
           i < rows.size() * (t + 1) / workers; ++i)
        ++counts[t][((reverse ? rows[i].v : rows[i].u) >> shift) & 2047];
    });
    size_t position = 0;
    for (size_t b = 0; b < 2048; ++b)
      for (size_t t = 0; t < workers; ++t) {
        positions[t][b] = position;
        position += counts[t][b];
      }
    parallel(workers, [&](size_t t) {
      auto &cursor = positions[t];
      for (size_t i = rows.size() * t / workers;
           i < rows.size() * (t + 1) / workers; ++i) {
        Raw e = rows[i];
        scratch[cursor[((reverse ? e.v : e.u) >> shift) & 2047]++] = e;
      }
    });
    rows.swap(scratch);
  }
}
int main(int argc, char **argv) {
  try {
    const std::string data = argc > 1 ? argv[1] : "/data",
                      out = argc > 2 ? argv[2] : "/out";
    Mapping input(data + "/graph.txt"), query_file(data + "/queries.txt");
    const char *p = input.data;
    uint32_t n = number(p);
    ++p;
    uint32_t m = number(p);
    ++p;
    size_t builders = input.size > 1000000 ? 6 : 1;
    std::vector<const char *> boundary(builders + 1, input.data + input.size);
    boundary[0] = p;
    for (size_t t = 1; t < builders; ++t) {
      const char *q = p + (input.data + input.size - p) * t / builders;
      while (q < input.data + input.size && *q++ != '\n') {
      }
      boundary[t] = q;
    }
    std::vector<size_t> starts(builders + 1);
    parallel(builders, [&](size_t t) {
      starts[t + 1] = std::count(boundary[t], boundary[t + 1], '\n');
    });
    for (size_t t = 1; t <= builders; ++t)
      starts[t] += starts[t - 1];
    if (starts.back() != m)
      throw std::runtime_error("edge count mismatch");
    std::vector<Raw> raw(m);
    parallel(builders, [&](size_t t) {
      const char *q = boundary[t];
      size_t i = starts[t];
      while (q < boundary[t + 1]) {
        uint32_t u = number(q);
        ++q;
        uint32_t v = number(q);
        ++q;
        uint32_t w = number(q);
        ++q;
        raw[i++] = {u, v, w};
      }
    });
    std::array<std::vector<uint32_t>, 2> offsets{std::vector<uint32_t>(n + 1),
                                                 std::vector<uint32_t>(n + 1)};
    std::array<std::vector<Edge>, 2> edges;
    // Radix ordering replaces random CSR scatter writes with sequential
    // streams.
    for (size_t side = 0; side < 2; ++side) {
      radix_sort(raw, n, side == 1, builders);
      edges[side].resize(m);
      uint32_t node = 0;
      for (uint32_t i = 0; i < m; ++i) {
        Raw e = raw[i];
        uint32_t u = side ? e.v : e.u;
        while (node <= u)
          offsets[side][node++] = i;
        edges[side][i] = {side ? e.u : e.v, e.w};
      }
      while (node <= n)
        offsets[side][node++] = m;
    }
    std::vector<Raw>().swap(raw);
    // PROFILE_GRAPH_DONE
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    p = query_file.data;
    while (p < query_file.data + query_file.size) {
      uint32_t s = number(p);
      ++p;
      uint32_t t = number(p);
      ++p;
      queries.emplace_back(s, t);
    }
    std::vector<int64_t> answers(queries.size(), -1);
    std::atomic<size_t> next{0};
    size_t workers = input.size > 1000000 ? 4 : 1;
    if (const char *env = std::getenv("SD_THREADS"))
      workers = std::clamp(std::atoi(env), 1, 12);
    parallel(workers, [&](size_t) {
      std::array<SearchState, 2> state;
      std::array<RadixHeap, 2> heap;
      for (;;) {
        size_t i = next.fetch_add(1);
        if (i >= queries.size())
          break;
        auto [s, t] = queries[i];
        if (s == t) {
          answers[i] = 0;
          continue;
        }
        if (offsets[0][s] == offsets[0][s + 1] ||
            offsets[1][t] == offsets[1][t + 1])
          continue;
        state[0].clear();
        state[1].clear();
        heap[0].clear();
        heap[1].clear();
        state[0].relax(s, 0);
        state[1].relax(t, 0);
        heap[0].push(0, s);
        heap[1].push(0, t);
        uint64_t best = UINT64_MAX;
        // Every meeting candidate is a real directed s->t path. Stop when the
        // two unsettled minima cannot improve the best such path.
        while (heap[0].count && heap[1].count) {
          uint64_t a = heap[0].minimum(), b = heap[1].minimum();
          if (best != UINT64_MAX && a + b >= best)
            break;
          unsigned side = heap[0].count <= heap[1].count ? 0 : 1,
                   other = side ^ 1;
          auto [d, u] = heap[side].pop();
          if (d != state[side].get(u))
            continue;
          if (auto opposite = state[other].get(u); opposite != UINT64_MAX)
            best = std::min(best, d + opposite);
          for (uint32_t e = offsets[side][u]; e < offsets[side][u + 1]; ++e) {
            auto [v, w] = edges[side][e];
            uint64_t nd = d + w;
            if (state[side].relax(v, nd)) {
              heap[side].push(nd, v);
              if (auto opposite = state[other].get(v); opposite != UINT64_MAX)
                best = std::min(best, nd + opposite);
            }
          }
        }
        if (best != UINT64_MAX)
          answers[i] = best;
      }
    });
    // PROFILE_SEARCH_DONE
    std::string output;
    for (auto d : answers) {
      if (d < 0)
        output += "-1";
      else
        append(output, d);
      output += '\n';
    }
    std::filesystem::create_directories(out);
    write_file(out + "/distances.txt", output);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
