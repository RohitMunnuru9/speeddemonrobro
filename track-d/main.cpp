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
#include <span>
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
struct BlockStore {
  struct Block {
    uint32_t offset = 0, count = 0, capacity = 0;
  };
  std::unique_ptr<uint64_t[]> values;
  std::vector<Block> blocks;
  std::vector<std::vector<uint64_t>> overflow;
  void initialize(size_t count, uint32_t capacity) {
    values = std::make_unique_for_overwrite<uint64_t[]>(count * capacity);
    blocks.resize(count);
    for (size_t i = 0; i < count; ++i)
      blocks[i] = {uint32_t(i * capacity), 0, capacity};
  }
  void append(size_t index, uint64_t value) {
    auto &block = blocks[index];
    if (block.capacity && block.count < block.capacity)
      values[block.offset + block.count] = value;
    else {
      if (block.capacity) {
        auto first = values.get() + block.offset;
        overflow.emplace_back(first, first + block.count);
        block.offset = overflow.size() - 1;
        block.capacity = 0;
      }
      overflow[block.offset].push_back(value);
    }
    ++block.count;
  }
  std::span<const uint64_t> range(size_t index) const {
    const auto &block = blocks[index];
    if (!block.count)
      return {};
    return block.capacity ? std::span<const uint64_t>(
                                values.get() + block.offset, block.count)
                          : std::span<const uint64_t>(overflow[block.offset]);
  }
};
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
    // The coarse vertex block supplies high vertex bits. The remaining local
    // ID, full uint32 weight and opposite ID fit losslessly in one uint64.
    unsigned shift = std::min(10u, 32u - std::bit_width(n - 1));
    uint32_t mask = (uint32_t{1} << shift) - 1;
    size_t blocks = (uint64_t(n) + mask) >> shift;
    std::vector<std::array<BlockStore, 2>> stores(builders);
    parallel(builders, [&](size_t t) {
      size_t expected = uint64_t(m) * (boundary[t + 1] - boundary[t]) /
                        std::max<ptrdiff_t>(1, input.data + input.size - p);
      uint32_t capacity = expected / blocks * 6 / 5 + 32;
      stores[t][0].initialize(blocks, capacity);
      stores[t][1].initialize(blocks, capacity);
      const char *q = boundary[t];
      while (q < boundary[t + 1]) {
        uint32_t u = number(q);
        ++q;
        uint32_t v = number(q);
        ++q;
        uint32_t w = number(q);
        ++q;
        stores[t][0].append(u >> shift, (uint64_t(v) << (32 + shift)) |
                                            (uint64_t(w) << shift) |
                                            (u & mask));
        stores[t][1].append(v >> shift, (uint64_t(u) << (32 + shift)) |
                                            (uint64_t(w) << shift) |
                                            (v & mask));
      }
    });
    std::array<std::vector<uint32_t>, 2> coarse{
        std::vector<uint32_t>(blocks + 1), std::vector<uint32_t>(blocks + 1)};
    for (size_t side = 0; side < 2; ++side) {
      uint32_t cursor = 0;
      for (size_t b = 0; b < blocks; ++b) {
        coarse[side][b] = cursor;
        for (size_t t = 0; t < builders; ++t)
          cursor += stores[t][side].blocks[b].count;
      }
      coarse[side].back() = cursor;
      if (cursor != m)
        throw std::runtime_error("edge count mismatch");
    }
    std::array<std::unique_ptr<uint32_t[]>, 2> offsets;
    unsigned weight_bits = 31 - std::min(31u, unsigned(std::bit_width(n - 1)));
    uint32_t weight_mask = (uint64_t{1} << weight_bits) - 1;
    uint32_t neighbor_mask = (uint64_t{1} << (31 - weight_bits)) - 1;
    std::array<std::unique_ptr<uint32_t[]>, 2> edges;
    std::array<std::unique_ptr<uint64_t[]>, 2> wide;
    for (size_t side = 0; side < 2; ++side) {
      offsets[side] = std::make_unique_for_overwrite<uint32_t[]>(size_t(n) + 1);
      edges[side] = std::make_unique_for_overwrite<uint32_t[]>(m);
      std::vector<uint32_t> wide_counts(blocks + 1);
      parallel(builders, [&](size_t t) {
        std::array<uint32_t, 1024> local;
        for (size_t b = t; b < blocks; b += builders) {
          local.fill(0);
          uint32_t exceptions = 0;
          for (size_t worker = 0; worker < builders; ++worker)
            for (auto row : stores[worker][side].range(b)) {
              ++local[row & mask];
              exceptions += uint32_t(row >> (32 + shift)) > neighbor_mask ||
                            uint32_t(row >> shift) > weight_mask;
            }
          wide_counts[b] = exceptions;
          uint32_t cursor = coarse[side][b];
          for (size_t j = 0; j <= mask && (b << shift) + j < n; ++j) {
            offsets[side][(b << shift) + j] = cursor;
            cursor += local[j];
          }
        }
      });
      uint32_t total = 0;
      for (size_t b = 0; b < blocks; ++b) {
        uint32_t count = wide_counts[b];
        wide_counts[b] = total;
        total += count;
      }
      wide_counts.back() = total;
      if (total >= uint32_t{1} << 31)
        throw std::runtime_error("wide edge index overflow");
      wide[side] = std::make_unique_for_overwrite<uint64_t[]>(total);
      parallel(builders, [&](size_t t) {
        std::array<uint32_t, 1024> cursor;
        for (size_t b = t; b < blocks; b += builders) {
          for (size_t j = 0; j <= mask && (b << shift) + j < n; ++j)
            cursor[j] = offsets[side][(b << shift) + j];
          uint32_t exception = wide_counts[b];
          for (size_t worker = 0; worker < builders; ++worker)
            for (auto row : stores[worker][side].range(b)) {
              uint32_t neighbor = row >> (32 + shift), weight = row >> shift,
                       code;
              if (neighbor <= neighbor_mask && weight <= weight_mask)
                code = (neighbor << weight_bits) | weight;
              else {
                wide[side][exception] = (uint64_t(neighbor) << 32) | weight;
                code = (uint32_t{1} << 31) | exception++;
              }
              edges[side][cursor[row & mask]++] = code;
            }
        }
      });
      offsets[side][n] = m;
      for (auto &worker : stores)
        worker[side] = BlockStore{};
    }
    auto decode = [&](size_t side, uint32_t code) {
      if (!(code >> 31))
        return Edge{code >> weight_bits, code & weight_mask};
      uint64_t value = wide[side][code & 0x7fffffffU];
      return Edge{uint32_t(value >> 32), uint32_t(value)};
    };
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
            auto [v, w] = decode(side, edges[side][e]);
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
