#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <immintrin.h>
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
struct alignas(64) Name {
  std::atomic<uint64_t> prefix{0};
  std::array<uint64_t, 3> tail{};
  std::array<std::atomic<uint64_t>, 4> countries{};
};
// Short encoded payloads live directly with their masks. A length byte makes
// the padded 32-byte key injective even when a payload contains zero bytes.
// Longer keys and crowded probe chains retain an unbounded exact fallback.
struct NameTable {
  size_t capacity;
  std::unique_ptr<Name[]> entries;
  std::array<std::atomic<uint64_t>, 4> empty{};
  std::mutex mutex;
  std::unordered_map<std::string_view, std::array<uint64_t, 4>> overflow;
  explicit NameTable(size_t size)
      : capacity(size), entries(std::make_unique<Name[]>(size)) {}
  __attribute__((target("avx2"))) bool mark(std::string_view text,
                                            size_t country, const char *end) {
    uint64_t bit = uint64_t{1} << (country % 64);
    if (text.empty())
      return !(empty[country / 64].fetch_or(bit, std::memory_order_relaxed) &
               bit);
    auto fallback = [&] {
      std::lock_guard lock(mutex);
      auto &word = overflow[text][country / 64];
      bool added = !(word & bit);
      word |= bit;
      return added;
    };
    if (text.size() > 31)
      return fallback();
    std::array<uint64_t, 4> key{};
    if (end - text.data() >= 32) {
      const auto indices = _mm256_setr_epi8(
          0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
          20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
      auto keep = _mm256_cmpgt_epi8(_mm256_set1_epi8(text.size()), indices);
      auto bytes =
          _mm256_loadu_si256(reinterpret_cast<const __m256i *>(text.data()));
      _mm256_storeu_si256(reinterpret_cast<__m256i *>(key.data()),
                          _mm256_and_si256(bytes, keep));
    } else
      memcpy(key.data(), text.data(), text.size());
    reinterpret_cast<unsigned char *>(key.data())[31] = text.size();
    if (key[0] < 2)
      return fallback();
    uint64_t last = 0;
    if (text.size() >= 8)
      memcpy(&last, text.data() + text.size() - 8, 8);
    uint64_t hash = key[0] ^ std::rotl(last, 29) ^ text.size();
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    size_t slot = hash & (capacity - 1);
    for (size_t probe = 0; probe < 128; ++probe) {
      auto &entry = entries[slot];
      uint64_t present = entry.prefix.load(std::memory_order_acquire);
      if (!present && entry.prefix.compare_exchange_strong(
                          present, 1, std::memory_order_acq_rel)) {
        entry.tail = {key[1], key[2], key[3]};
        entry.prefix.store(key[0], std::memory_order_release);
        present = key[0];
      }
      while (present == 1)
        present = entry.prefix.load(std::memory_order_acquire);
      if (present == key[0] && entry.tail == std::array{key[1], key[2], key[3]})
        return !(entry.countries[country / 64].fetch_or(
                     bit, std::memory_order_relaxed) &
                 bit);
      slot = (slot + 1) & (capacity - 1);
    }
    return fallback();
  }
};
struct Field {
  std::string_view bytes;
  size_t length;
};
__attribute__((target("avx2"))) const char *find_quote(const char *p,
                                                       const char *end) {
  if (end - p >= 32) {
    auto block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
    uint32_t quotes =
        _mm256_movemask_epi8(_mm256_cmpeq_epi8(block, _mm256_set1_epi8('"')));
    if (quotes)
      return p + std::countr_zero(quotes);
    p += 32;
  }
  return static_cast<const char *>(memchr(p, '"', end - p));
}
Field field(const char *&p, const char *end) {
  const char *start = p;
  size_t escapes = 0;
  if (*p == '"') {
    start = ++p;
    for (;;) {
      const char *quote = find_quote(p, end);
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
    NameTable names(input.size > 1000000 ? 131072 : 2048);
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
        g.names += names.mark(name.bytes, c, end);
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
