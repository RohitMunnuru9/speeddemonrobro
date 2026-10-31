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
#include <mutex>
#include <string_view>
#include <unordered_map>

struct Country {
  uint64_t count = 0, ids = 0, name_bytes = 0, note_bytes = 0, names = 0;
  int64_t sum = 0, lo = INT64_MAX, hi = INT64_MIN;
};
struct Name {
  uint32_t offset = 0, length = 0;
  std::array<std::atomic<uint64_t>, 4> countries{};
  std::atomic<uint64_t> hash{0};
};
// Collision checks always compare the actual bytes; hashes are not identities.
struct NameTable {
  size_t capacity;
  std::unique_ptr<Name[]> entries;
  std::unique_ptr<char[]> arena;
  std::atomic<size_t> arena_used{0};
  std::mutex mutex;
  std::unordered_map<std::string_view, std::array<uint64_t, 4>> overflow;
  NameTable(size_t size, size_t bytes)
      : capacity(size), entries(std::make_unique<Name[]>(size)),
        arena(std::make_unique_for_overwrite<char[]>(bytes)) {
    if (bytes > UINT32_MAX)
      throw std::runtime_error("CSV exceeds key arena bound");
  }
  bool mark(std::string_view text, size_t country) {
    uint64_t first = 0, last = 0;
    if (text.size() < 8)
      memcpy(&first, text.data(), text.size());
    else {
      memcpy(&first, text.data(), 8);
      memcpy(&last, text.data() + text.size() - 8, 8);
    }
    uint64_t hash = first ^ std::rotl(last, 29) ^ text.size();
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    if (hash < 2)
      hash += 2;
    size_t slot = hash & (capacity - 1);
    uint64_t bit = uint64_t{1} << (country % 64);
    for (size_t probe = 0; probe < 128; ++probe) {
      auto &entry = entries[slot];
      uint64_t present = entry.hash.load(std::memory_order_acquire);
      if (!present && entry.hash.compare_exchange_strong(
                          present, 1, std::memory_order_acq_rel)) {
        // Copy each distinct key once into compact storage. Comparing against
        // scattered first occurrences in the 200 MB input caused cache misses.
        // Unique payload bytes cannot exceed the input file's byte count.
        char *owned = arena.get() + arena_used.fetch_add(
                                        text.size(), std::memory_order_relaxed);
        memcpy(owned, text.data(), text.size());
        entry.offset = owned - arena.get();
        entry.length = text.size();
        entry.hash.store(hash, std::memory_order_release);
        present = hash;
      }
      while (present == 1)
        present = entry.hash.load(std::memory_order_acquire);
      if (present == hash &&
          std::string_view(arena.get() + entry.offset, entry.length) == text) {
        auto &membership = entry.countries[country / 64];
        // Bits only change from zero to one. An observed one needs no write;
        // fetch_or arbitrates exactly once when several workers see zero.
        if (membership.load(std::memory_order_relaxed) & bit)
          return false;
        return !(membership.fetch_or(bit, std::memory_order_relaxed) & bit);
      }
      slot = (slot + 1) & (capacity - 1);
    }
    // Unbounded exact fallback for unusually large/skewed vocabularies. Slots
    // are never removed, so a name that overflows follows this path thereafter.
    std::lock_guard lock(mutex);
    auto &word = overflow[text][country / 64];
    bool added = !(word & bit);
    word |= bit;
    return added;
  }
};
struct Field {
  std::string_view bytes;
  size_t length;
};
Field field(const char *&p, const char *end) {
  const char *start = p;
  size_t escapes = 0;
  if (*p == '"') {
    start = ++p;
    for (;;) {
      const char *quote = static_cast<const char *>(memchr(p, '"', end - p));
      if (!quote)
        throw std::runtime_error("unterminated quote");
      p = quote + 1;
      if (p == end || *p != '"')
        return {{start, size_t(quote - start)},
                size_t(quote - start) - escapes};
      ++p;
      ++escapes;
    }
  }
  while (p < end && *p != ',' && *p != '\r' && *p != '\n')
    ++p;
  return {{start, size_t(p - start)}, size_t(p - start)};
}
void money(std::string &out, int64_t value) {
  if (value < 0) {
    out += '-';
    value = -value;
  }
  append(out, value / 100);
  out += '.';
  out += char('0' + value / 10 % 10);
  out += char('0' + value % 10);
}
int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "/data",
                out = argc > 2 ? argv[2] : "/out";
    Mapping lookup(data + "/regions.csv"), input(data + "/records.csv");
    std::array<int16_t, 26 * 26 * 26> country_index;
    country_index.fill(-1);
    std::vector<std::string> codes;
    std::vector<uint8_t> regions(1000000, UINT8_MAX);
    const char *p = lookup.data, *end = p + lookup.size;
    while (p < end && *p++ != '\n') {
    }
    while (p < end) {
      uint32_t id = number(p);
      ++p;
      uint32_t code = (p[0] - 'A') * 676 + (p[1] - 'A') * 26 + p[2] - 'A';
      auto &index = country_index[code];
      if (index < 0) {
        index = codes.size();
        codes.emplace_back(p, 3);
      }
      p += 4;
      if (id >= regions.size())
        regions.resize(id + 1, UINT8_MAX);
      regions[id] = index;
    }
    const size_t unmatched = codes.size();
    codes.emplace_back("_UNMATCHED");
    if (codes.size() > 255)
      throw std::runtime_error("country limit exceeded");
    p = input.data;
    end = p + input.size;
    while (p < end && *p++ != '\n') {
    }
    size_t workers = input.size > 1000000 ? 6 : 1;
    if (const char *env = std::getenv("SD_THREADS"))
      workers = std::clamp(std::atoi(env), 1, 12);
    std::vector<const char *> boundary(workers + 1, end);
    boundary[0] = p;
    // Section 2.2 guarantees CR only appears in CRLF record terminators.
    // A bare LF cannot delimit chunks because notes may contain it.
    for (size_t i = 1; i < workers; ++i) {
      const char *q = input.data + input.size * i / workers;
      const char *cr = static_cast<const char *>(memchr(q, '\r', end - q));
      boundary[i] = cr ? cr + 2 : end;
    }
    std::vector<std::vector<Country>> groups(
        workers, std::vector<Country>(codes.size()));
    NameTable names(input.size > 1000000 ? 131072 : 2048, input.size);
    parallel(workers, [&](size_t t) {
      const char *q = boundary[t];
      while (q < boundary[t + 1]) {
        if (static_cast<unsigned char>(*q) == 0xEF)
          q += 3;
        uint64_t id = number(q);
        ++q;
        Field name = field(q, end);
        ++q;
        Field note = field(q, end);
        ++q;
        uint32_t region = number(q);
        ++q;
        bool negative = *q == '-';
        if (negative)
          ++q;
        int64_t cents = number(q) * 100;
        ++q;
        cents += (q[0] - '0') * 10 + q[1] - '0';
        q += 2;
        if (negative)
          cents = -cents;
        if (q < end && *q == '\r')
          ++q;
        if (q < end && *q == '\n')
          ++q;
        size_t c = region < regions.size() && regions[region] != UINT8_MAX
                       ? regions[region]
                       : unmatched;
        auto &g = groups[t][c];
        ++g.count;
        g.ids += id;
        g.sum += cents;
        g.lo = std::min(g.lo, cents);
        g.hi = std::max(g.hi, cents);
        g.name_bytes += name.length;
        g.note_bytes += note.length;
        // CSV quote escaping is injective: exact encoded-name equality is
        // exact decoded-name equality. Empty quoted/unquoted views both have
        // length 0.
        g.names += names.mark(name.bytes, c);
      }
    });
    for (size_t t = 1; t < workers; ++t) {
      for (size_t c = 0; c < codes.size(); ++c) {
        auto &g = groups[0][c];
        const auto &other = groups[t][c];
        g.count += other.count;
        g.ids += other.ids;
        g.sum += other.sum;
        g.lo = std::min(g.lo, other.lo);
        g.hi = std::max(g.hi, other.hi);
        g.name_bytes += other.name_bytes;
        g.note_bytes += other.note_bytes;
        g.names += other.names;
      }
    }
    std::vector<size_t> order(codes.size());
    for (size_t i = 0; i < order.size(); ++i)
      order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return codes[a] < codes[b]; });
    std::string output;
    output.reserve(20000);
    for (size_t c : order) {
      const auto &g = groups[0][c];
      if (!g.count)
        continue;
      output += codes[c];
      output += ' ';
      append(output, g.count);
      output += ' ';
      append(output, g.ids);
      output += ' ';
      money(output, g.sum);
      output += ' ';
      money(output, g.lo);
      output += ' ';
      money(output, g.hi);
      output += ' ';
      append(output, g.names);
      output += ' ';
      append(output, g.name_bytes);
      output += ' ';
      append(output, g.note_bytes);
      output += '\n';
    }
    std::filesystem::create_directories(out);
    write_file(out + "/aggregates.txt", output);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
