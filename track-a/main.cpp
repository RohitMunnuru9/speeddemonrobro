#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <immintrin.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <string_view>
#include <unordered_map>
using View = std::string_view;
uint64_t name_hash(View s) {
  uint64_t a = 0, b = 0, c = 0;
  if (s.size() < 8)
    memcpy(&a, s.data(), s.size());
  else {
    memcpy(&a, s.data(), 8);
    memcpy(&b, s.data() + s.size() - 8, 8);
    memcpy(&c, s.data() + (s.size() - 8) / 2, 8);
  }
  uint64_t h = a ^ std::rotl(b, 21) ^ std::rotl(c, 43) ^ s.size();
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return h;
}
struct Hash {
  size_t operator()(View s) const { return name_hash(s); }
};
constexpr size_t name_slots = 4096;
struct Catalog {
  struct alignas(128) Name {
    char bytes[125]{};
    uint8_t size = 0;
  };
  std::array<std::atomic<uint64_t>, name_slots> tags{};
  std::array<Name, name_slots> names;
  std::atomic<size_t> published{0};
  std::mutex overflow_mutex, sort_mutex;
  std::deque<std::string> extra;
  std::unordered_map<View, uint32_t, Hash> overflow;
  size_t sorted_version = 0;
  std::shared_ptr<std::vector<uint32_t>> sorted =
      std::make_shared<std::vector<uint32_t>>();
  __attribute__((cold, noinline)) uint32_t fallback(View name) {
    std::lock_guard lock(overflow_mutex);
    auto it = overflow.find(name);
    if (it != overflow.end())
      return it->second;
    if (extra.size() >= uint32_t{0x7fffffff} - name_slots)
      throw std::runtime_error("endpoint index overflow");
    uint32_t id = name_slots + extra.size();
    extra.emplace_back(name);
    overflow.emplace(extra.back(), id);
    published.fetch_add(1, std::memory_order_release);
    return id;
  }
  uint32_t intern(View name) {
    if (name.size() > 125)
      return fallback(name);
    uint64_t h = name_hash(name);
    if (h < 2)
      h += 2;
    size_t slot = h & (name_slots - 1);
    for (unsigned probe = 0; probe < 128; ++probe) {
      auto &tag = tags[slot];
      uint64_t value = tag.load(std::memory_order_acquire);
      if (!value &&
          tag.compare_exchange_strong(value, 1, std::memory_order_acq_rel)) {
        memcpy(names[slot].bytes, name.data(), name.size());
        names[slot].size = name.size();
        // The version precedes the ready tag. A snapshot made during
        // publication is incomplete and must be rescanned even if its version
        // stays unchanged.
        published.fetch_add(1, std::memory_order_release);
        tag.store(h, std::memory_order_release);
        return slot;
      }
      while (value == 1) {
        _mm_pause();
        value = tag.load(std::memory_order_acquire);
      }
      if (value == h && names[slot].size == name.size() &&
          View(names[slot].bytes, name.size()) == name)
        return slot;
      slot = (slot + 1) & (name_slots - 1);
    }
    return fallback(name);
  }
  View name(uint32_t id) {
    if (id < name_slots)
      return {names[id].bytes, names[id].size};
    std::lock_guard lock(overflow_mutex);
    return extra[id - name_slots];
  }
  std::shared_ptr<const std::vector<uint32_t>> order() {
    std::lock_guard lock(sort_mutex);
    size_t version = published.load(std::memory_order_acquire);
    if (version != sorted_version || sorted->size() != version) {
      auto ids = std::make_shared<std::vector<uint32_t>>();
      for (uint32_t id = 0; id < name_slots; ++id)
        if (tags[id].load(std::memory_order_acquire) > 1)
          ids->push_back(id);
      {
        std::lock_guard other(overflow_mutex);
        for (size_t i = 0; i < extra.size(); ++i)
          ids->push_back(name_slots + i);
      }
      std::sort(ids->begin(), ids->end(),
                [&](uint32_t a, uint32_t b) { return name(a) < name(b); });
      sorted = std::move(ids);
      sorted_version = version;
    }
    return sorted;
  }
};
constexpr size_t chunk = 256, slots = 512,
                 region_bytes = chunk * slots * sizeof(uint64_t);
struct Region {
  std::atomic<Region *> next{nullptr};
  std::array<std::atomic<uint16_t>, slots> counts{};
  uint64_t *data;
  Region() {
    void *memory = mmap(nullptr, region_bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
      throw std::bad_alloc();
    data = ::new (memory) uint64_t[chunk * slots];
  }
  ~Region() { munmap(data, region_bytes); }
  static Region *ensure(std::atomic<Region *> &link) {
    Region *old = link.load(std::memory_order_acquire);
    if (old)
      return old;
    auto node = std::make_unique<Region>();
    if (link.compare_exchange_strong(old, node.get(),
                                     std::memory_order_acq_rel))
      return node.release();
    return old;
  }
};
struct Batch {
  Region *region = nullptr;
  size_t slot = 0, count = 0;
  uint64_t *rows = nullptr;
};
struct alignas(64) Hour {
  std::atomic<unsigned> state{0}, writers{0};
  std::atomic<size_t> next{0};
  std::atomic<Region *> head{nullptr};
  Batch reserve() {
    size_t index = next.fetch_add(1, std::memory_order_relaxed);
    Region *node = Region::ensure(head);
    for (size_t i = 0; i < index / slots; ++i)
      node = Region::ensure(node->next);
    size_t slot = index % slots;
    return {node, slot, 0, node->data + slot * chunk};
  }
  void publish(Batch &batch) {
    if (batch.count) {
      batch.region->counts[batch.slot].store(batch.count,
                                             std::memory_order_release);
      batch = {};
    }
  }
  ~Hour() {
    auto node = head.load();
    while (node) {
      auto next = node->next.load();
      delete node;
      node = next;
    }
  }
};
unsigned hour_of(const char *p) {
  unsigned day = p[9] - '0' + (p[6] != '0');
  return day * 24 + (p[11] - '0') * 10 + p[12] - '0' - 18;
}
uint32_t decimal_end(const char *end, unsigned length) {
  uint64_t d;
  memcpy(&d, end - 8, 8);
  d &= 0x0f0f0f0f0f0f0f0fULL;
  if (length < 8)
    d &= UINT64_MAX << ((8 - length) * 8);
  d = (d * 10 + (d >> 8)) & 0x00ff00ff00ff00ffULL;
  d = (d * 100 + (d >> 16)) & 0x0000ffff0000ffffULL;
  d = (d * 10000 + (d >> 32)) & 0xffffffffULL;
  if (length > 8) {
    unsigned prefix = end[-ptrdiff_t(length)] - '0';
    if (length == 10)
      prefix = prefix * 10 + end[-ptrdiff_t(length) + 1] - '0';
    d += uint64_t(prefix) * 100000000;
  }
  return d;
}
struct Parsed {
  View name;
  uint32_t latency;
  bool error;
  const char *next;
};
inline Parsed parse_line(const char *line, const char *end) {
  const char *p = line + 28;
  if (end - p >= 64) {
    auto a = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p)),
         b = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 32));
    uint64_t nl = uint32_t(_mm256_movemask_epi8(
                      _mm256_cmpeq_epi8(a, _mm256_set1_epi8('\n')))) |
                  (uint64_t(uint32_t(_mm256_movemask_epi8(
                       _mm256_cmpeq_epi8(b, _mm256_set1_epi8('\n')))))
                   << 32);
    if (nl) {
      uint64_t sp = uint32_t(_mm256_movemask_epi8(
                        _mm256_cmpeq_epi8(a, _mm256_set1_epi8(' ')))) |
                    (uint64_t(uint32_t(_mm256_movemask_epi8(
                         _mm256_cmpeq_epi8(b, _mm256_set1_epi8(' ')))))
                     << 32);
      unsigned method = std::countr_zero(sp);
      sp &= sp - 1;
      unsigned name = std::countr_zero(sp), last = std::countr_zero(nl);
      return {{p + method + 1, name - method - 1},
              decimal_end(p + last, last - name - 5),
              p[name + 1] == '5',
              p + last + 1};
    }
  }
  const char *name = static_cast<const char *>(memchr(p, ' ', end - p)) + 1,
             *stop = static_cast<const char *>(memchr(name, ' ', end - name));
  bool error = stop[1] == '5';
  p = stop + 5;
  uint32_t latency = number(p);
  return {{name, size_t(stop - name)}, latency, error, p + 1};
}
std::string finish_hour(unsigned h, Hour &hour, Catalog &catalog) {
  Region *first = hour.head.exchange(nullptr, std::memory_order_acquire);
  if (!first)
    return {};
  std::vector<std::unique_ptr<Region>> regions;
  for (auto node = first; node;) {
    auto next = node->next.load();
    regions.emplace_back(node);
    node = next;
  }
  auto order = catalog.order();
  size_t size = *std::max_element(order->begin(), order->end()) + 1;
  std::vector<size_t> starts(size + 1);
  std::vector<uint32_t> errors(size);
  for (auto &region : regions)
    for (size_t slot = 0; slot < slots; ++slot) {
      size_t count = region->counts[slot].load(std::memory_order_acquire);
      for (size_t i = 0; i < count; ++i) {
        uint64_t row = region->data[slot * chunk + i];
        uint32_t id = (row >> 32) & 0x7fffffff;
        ++starts[id + 1];
        errors[id] += row >> 63;
      }
    }
  std::partial_sum(starts.begin(), starts.end(), starts.begin());
  auto cursor = starts;
  std::vector<uint32_t> values(starts.back());
  for (auto &region : regions)
    for (size_t slot = 0; slot < slots; ++slot) {
      size_t count = region->counts[slot].load(std::memory_order_acquire);
      for (size_t i = 0; i < count; ++i) {
        uint64_t row = region->data[slot * chunk + i];
        uint32_t id = (row >> 32) & 0x7fffffff;
        values[cursor[id]++] = row;
      }
    }
  regions.clear();
  unsigned absolute = h + 18, day = absolute / 24;
  char stamp[32];
  snprintf(stamp, sizeof(stamp), "2026-%02u-%02uT%02u:00:00Z ",
           day < 2 ? 10 : 11, day < 2 ? 30 + day : day - 1, absolute % 24);
  std::string out;
  out.reserve(160000);
  for (auto id : *order) {
    size_t n = starts[id + 1] - starts[id];
    if (!n)
      continue;
    auto begin = values.begin() + starts[id], end = begin + n;
    std::array<uint32_t, 3> quantiles;
    if (n < 32) {
      std::sort(begin, end);
      size_t k = 0;
      for (size_t p : {50, 95, 99})
        quantiles[k++] = begin[(p * n + 99) / 100 - 1];
    } else {
      size_t start = 0, k = 0;
      for (size_t p : {50, 95, 99}) {
        size_t rank = (p * n + 99) / 100 - 1;
        std::nth_element(begin + start, begin + rank, end);
        quantiles[k++] = begin[rank];
        start = rank;
      }
    }
    out += stamp;
    out += catalog.name(id);
    out += ' ';
    append(out, n);
    for (auto q : quantiles) {
      out += ' ';
      append(out, q);
    }
    out += ' ';
    append(out, errors[id]);
    out += ' ';
    append(out, uint64_t(errors[id]) * 1000000 / n);
    out += '\n';
  }
  return out;
}
struct Piece {
  size_t begin, end;
  unsigned hour;
};
struct Progress {
  std::mutex mutex;
  std::condition_variable changed;
  size_t epoch = 0;
  void signal() {
    std::lock_guard lock(mutex);
    ++epoch;
    changed.notify_all();
  }
};
bool run_pass(const std::string &data, const std::string &out, size_t workers,
              bool early) {
  Mapping input(data + "/requests.log");
  std::vector<Piece> pieces;
  std::vector<size_t> boundary{0};
  size_t count = std::min(workers * 8, std::max(size_t{1}, input.size / 65536));
  for (size_t i = 1; i < count; ++i) {
    size_t p = input.size * i / count;
    const char *nl =
        static_cast<const char *>(memchr(input.data + p, '\n', input.size - p));
    boundary.push_back(nl ? nl - input.data + 1 : input.size);
  }
  boundary.push_back(input.size);
  for (size_t i = 0; i < count; ++i)
    if (boundary[i] < boundary[i + 1])
      pieces.push_back(
          {boundary[i], boundary[i + 1], hour_of(input.data + boundary[i])});
  auto catalog = std::make_unique<Catalog>();
  std::array<Hour, 72> hours;
  std::vector<std::atomic<bool>> done(pieces.size());
  std::atomic<size_t> next_piece{0}, front{0}, next_hour{0};
  std::atomic<bool> redo{false};
  Progress progress;
  std::mutex output_mutex;
  std::condition_variable output_ready;
  std::array<std::unique_ptr<std::string>, 72> results;
  size_t alive = 0;
  auto stop = [&] {
    redo.store(true);
    progress.signal();
    {
      std::lock_guard lock(output_mutex);
    }
    output_ready.notify_all();
  };
  std::vector<std::exception_ptr> failures(workers);
  std::vector<std::jthread> team;
  std::filesystem::create_directories(out);
  FILE *raw = fopen((out + "/summary.txt").c_str(), "wb");
  if (!raw)
    throw std::runtime_error("open output failed");
  std::unique_ptr<FILE, decltype(&fclose)> file(raw, fclose);
  try {
    for (size_t t = 0; t < workers; ++t) {
      {
        std::lock_guard lock(output_mutex);
        ++alive;
      }
      try {
        team.emplace_back([&, t] {
          try {
            for (;;) {
              if (redo.load(std::memory_order_relaxed))
                break;
              size_t current = front.load(std::memory_order_acquire),
                     cutoff = current == pieces.size() ? 72
                              : early ? (pieces[current].hour
                                             ? pieces[current].hour - 1
                                             : 0)
                                      : 0;
              size_t h = next_hour.load(std::memory_order_relaxed);
              if (h < cutoff && next_hour.compare_exchange_strong(h, h + 1)) {
                hours[h].state.store(1);
                while (hours[h].writers.load() &&
                       !redo.load(std::memory_order_relaxed))
                  std::this_thread::yield();
                if (redo.load(std::memory_order_relaxed))
                  break;
                auto text = std::make_unique<std::string>(
                    finish_hour(h, hours[h], *catalog));
                hours[h].state.store(2, std::memory_order_release);
                {
                  std::lock_guard lock(output_mutex);
                  results[h] = std::move(text);
                }
                output_ready.notify_one();
                continue;
              }
              size_t i = next_piece.fetch_add(1, std::memory_order_relaxed);
              if (i >= pieces.size()) {
                if (current == pieces.size()) {
                  if (h >= 72)
                    break;
                  continue;
                }
                std::unique_lock lock(progress.mutex);
                size_t epoch = progress.epoch;
                if (front.load(std::memory_order_acquire) < pieces.size() &&
                    !redo.load())
                  progress.changed.wait(lock, [&] {
                    return progress.epoch != epoch || redo.load();
                  });
                continue;
              }
              auto piece = pieces[i];
              std::array<Batch, 72> pending;
              std::array<bool, 72> active{};
              const char *p = input.data + piece.begin,
                         *end = input.data + piece.end;
              while (p < end) {
                unsigned h = hour_of(p);
                if (!active[h]) {
                  hours[h].writers.fetch_add(1);
                  if (hours[h].state.load()) {
                    hours[h].writers.fetch_sub(1);
                    stop();
                    break;
                  }
                  active[h] = true;
                }
                auto row = parse_line(p, end);
                p = row.next;
                uint32_t id = catalog->intern(row.name);
                auto &batch = pending[h];
                if (!batch.rows)
                  batch = hours[h].reserve();
                batch.rows[batch.count++] = uint64_t(row.latency) |
                                            (uint64_t(id) << 32) |
                                            (uint64_t(row.error) << 63);
                if (batch.count == chunk)
                  hours[h].publish(batch);
              }
              for (size_t h = 0; h < 72; ++h)
                if (active[h]) {
                  if (!redo.load(std::memory_order_relaxed))
                    hours[h].publish(pending[h]);
                  hours[h].writers.fetch_sub(1);
                }
              done[i].store(true, std::memory_order_release);
              size_t f = front.load(std::memory_order_acquire);
              while (f < done.size() &&
                     done[f].load(std::memory_order_acquire)) {
                if (front.compare_exchange_weak(f, f + 1))
                  ++f;
              }
              progress.signal();
            }
          } catch (...) {
            failures[t] = std::current_exception();
            stop();
          }
          {
            std::lock_guard lock(output_mutex);
            --alive;
          }
          output_ready.notify_all();
        });
      } catch (...) {
        std::lock_guard lock(output_mutex);
        --alive;
        throw;
      }
    }
    for (size_t h = 0; h < 72; ++h) {
      std::unique_lock lock(output_mutex);
      output_ready.wait(lock,
                        [&] { return results[h] || !alive || redo.load(); });
      if (redo.load())
        break;
      if (!results[h])
        throw std::runtime_error("incomplete hour output");
      auto text = std::move(results[h]);
      lock.unlock();
      if (fwrite(text->data(), 1, text->size(), raw) != text->size())
        throw std::runtime_error("write output failed");
    }
  } catch (...) {
    stop();
    for (auto &thread : team)
      thread.join();
    throw;
  }
  for (auto &thread : team)
    thread.join();
  for (auto error : failures)
    if (error)
      std::rethrow_exception(error);
  if (fflush(raw))
    throw std::runtime_error("flush output failed");
  return redo.load();
}
int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "/data",
                out = argc > 2 ? argv[2] : "/out";
    size_t workers = 12;
    if (auto env = getenv("SD_THREADS"))
      workers = std::clamp(atoi(env), 1, 12);
    if (run_pass(data, out, workers, true) &&
        run_pass(data, out, workers, false))
      throw std::runtime_error("safe pass rejected a record");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
