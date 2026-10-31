#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>
constexpr size_t hours = 72;
struct Slot {
  std::string_view text;
  uint64_t hash = 0;
  uint32_t id = 0;
};
uint64_t endpoint_hash(std::string_view s) {
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
  return h ? h : 1;
}
struct Index {
  std::vector<Slot> slots = std::vector<Slot>(4096);
  size_t used = 0;
  std::pair<uint32_t, bool> get(std::string_view name, uint32_t id,
                                bool insert) {
    uint64_t hash = endpoint_hash(name);
    size_t slot = hash & (slots.size() - 1);
    while (slots[slot].hash) {
      if (slots[slot].hash == hash && slots[slot].text == name)
        return {slots[slot].id, false};
      slot = (slot + 1) & (slots.size() - 1);
    }
    if (!insert)
      return {UINT32_MAX, false};
    slots[slot] = {name, hash, id};
    ++used;
    if (used * 4 > slots.size() * 3) {
      auto old = std::move(slots);
      slots.resize(old.size() * 2);
      used = 0;
      for (auto e : old)
        if (e.hash)
          get(e.text, e.id, true);
    }
    return {id, true};
  }
};
struct Endpoint {
  std::string_view name;
};
struct Group {
  uint32_t count = 0, errors = 0, capacity = 0;
  union Storage {
    uint32_t small[3];
    uint32_t *large;
    Storage() {}
  } storage;
  Group() = default;
  Group(const Group &) = delete;
  ~Group() {
    if (count > 3)
      std::free(storage.large);
  }
  void append(uint32_t value) {
    if (count < 3) {
      storage.small[count++] = value;
      return;
    }
    if (count == 3) {
      auto *buffer = static_cast<uint32_t *>(std::malloc(8 * sizeof(uint32_t)));
      if (!buffer)
        throw std::bad_alloc();
      std::copy_n(storage.small, 3, buffer);
      storage.large = buffer;
      capacity = 8;
    } else if (count == capacity) {
      auto *buffer = static_cast<uint32_t *>(
          std::realloc(storage.large, capacity * 2 * sizeof(uint32_t)));
      if (!buffer)
        throw std::bad_alloc();
      storage.large = buffer;
      capacity *= 2;
    }
    storage.large[count++] = value;
  }
  const uint32_t *begin() const {
    return count <= 3 ? storage.small : storage.large;
  }
};
struct Worker {
  using Page = std::array<Group, 64>;
  Index index;
  std::vector<Endpoint> endpoints;
  std::array<std::vector<std::unique_ptr<Page>>, hours> groups;
  Group &group(size_t id, size_t hour) {
    auto &pages = groups[hour];
    if (id / 64 >= pages.size())
      pages.resize(id / 64 + 1);
    if (!pages[id / 64])
      pages[id / 64] = std::make_unique<Page>();
    return (*pages[id / 64])[id % 64];
  }
  const Group *find(size_t id, size_t hour) const {
    const auto &pages = groups[hour];
    return id / 64 < pages.size() && pages[id / 64]
               ? &(*pages[id / 64])[id % 64]
               : nullptr;
  }
};
int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "/data",
                out = argc > 2 ? argv[2] : "/out";
    Mapping input(data + "/requests.log");
    size_t workers = input.size > 1000000 ? 12 : 1;
    if (const char *env = std::getenv("SD_THREADS"))
      workers = std::clamp(std::atoi(env), 1, 12);
    std::vector<const char *> boundary(workers + 1, input.data + input.size);
    boundary[0] = input.data;
    for (size_t i = 1; i < workers; ++i) {
      const char *p = input.data + input.size * i / workers;
      while (p < input.data + input.size && *p++ != '\n') {
      }
      boundary[i] = p;
    }
    std::vector<Worker> local(workers);
    // Append directly to per-worker groups. Small groups keep three latencies
    // inline; sparse hour/endpoint pages are allocated only when encountered.
    auto scan = [&](size_t thread) {
      auto &w = local[thread];
      w.endpoints.reserve(2048);
      const char *p = boundary[thread];
      while (p < boundary[thread + 1]) {
        const int day = p[6] == '0' ? p[9] - '0' : p[9] - '0' + 1;
        const int hour = day * 24 + (p[11] - '0') * 10 + p[12] - '0' - 18;
        p += 28;
        while (*p++ != ' ') {
        }
        const char *start = p;
        p = static_cast<const char *>(memchr(p, ' ', boundary[thread + 1] - p));
        std::string_view name(start, p - start);
        uint32_t id;
        auto [found, added] = w.index.get(name, w.endpoints.size(), true);
        if (added) {
          w.endpoints.emplace_back();
          w.endpoints.back().name = name;
        }
        id = found;
        auto &group = w.group(id, hour);
        bool error = p[1] == '5';
        p += 5;
        uint32_t latency = number(p);
        ++p;
        group.append(latency);
        group.errors += error;
      }
    };
    parallel(workers, scan);
    // PROFILE_PARSE_DONE
    std::vector<std::string_view> names;
    Index combined;
    for (auto &w : local)
      for (auto &e : w.endpoints)
        if (combined.get(e.name, 0, true).second)
          names.push_back(e.name);
    std::sort(names.begin(), names.end());
    // Resolve endpoint IDs once, rather than hashing the same name for all 72
    // hours.
    std::vector<std::array<uint32_t, 12>> references(names.size());
    for (auto &ids : references)
      ids.fill(UINT32_MAX);
    parallel(workers, [&](size_t worker) {
      for (size_t e = worker; e < names.size(); e += workers)
        for (size_t t = 0; t < workers; ++t) {
          auto id = local[t].index.get(names[e], 0, false).first;
          if (id != UINT32_MAX)
            references[e][t] = id;
        }
    });
    // PROFILE_MERGE_DONE
    // PROFILE_SCATTER_DONE
    std::vector<std::string> output(hours);
    std::atomic<size_t> next{0};
    parallel(workers, [&](size_t) {
      std::vector<uint32_t> values;
      for (;;) {
        size_t h = next.fetch_add(1);
        if (h >= hours)
          break;
        auto &text = output[h];
        text.reserve(160000);
        const int absolute = h + 18, day = absolute / 24;
        char timestamp[32];
        std::snprintf(timestamp, sizeof(timestamp),
                      "2026-%02d-%02dT%02d:00:00Z", day < 2 ? 10 : 11,
                      day < 2 ? 30 + day : day - 1, absolute % 24);
        for (size_t e = 0; e < names.size(); ++e) {
          values.clear();
          uint32_t errors = 0;
          for (size_t t = 0; t < workers; ++t) {
            uint32_t id = references[e][t];
            const auto *group = local[t].find(id, h);
            if (!group)
              continue;
            values.insert(values.end(), group->begin(),
                          group->begin() + group->count);
            errors += group->errors;
          }
          uint32_t n = values.size();
          if (!n)
            continue;
          auto first = values.begin(), last = values.end();
          std::array<uint32_t, 3> quantiles;
          if (n < 32) {
            std::sort(first, last);
            for (size_t k = 0; k < 3; ++k)
              quantiles[k] =
                  first[(std::array{50, 95, 99}[k] * uint64_t(n) + 99) / 100 -
                        1];
          } else {
            auto start = first;
            for (size_t k = 0; k < 3; ++k) {
              auto rank = first +
                          (std::array{50, 95, 99}[k] * uint64_t(n) + 99) / 100 -
                          1;
              std::nth_element(start, rank, last);
              quantiles[k] = *rank;
              start = rank;
            }
          }
          text += timestamp;
          text += ' ';
          text += names[e];
          text += ' ';
          append(text, n);
          for (auto q : quantiles) {
            text += ' ';
            append(text, q);
          }
          text += ' ';
          append(text, errors);
          text += ' ';
          append(text, uint64_t(errors) * 1000000 / n);
          text += '\n';
        }
        for (auto &worker : local)
          worker.groups[h].clear();
      }
    });
    // PROFILE_FORMAT_DONE
    std::filesystem::create_directories(out);
    auto close = [](FILE *file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(close)> file(
        std::fopen((out + "/summary.txt").c_str(), "wb"), close);
    if (!file)
      throw std::runtime_error("cannot open summary");
    for (const auto &part : output)
      if (std::fwrite(part.data(), 1, part.size(), file.get()) != part.size())
        throw std::runtime_error("cannot write summary");
    if (std::fflush(file.get()))
      throw std::runtime_error("cannot flush summary");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
