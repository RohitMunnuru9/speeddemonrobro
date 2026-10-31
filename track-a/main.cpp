#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <iostream>
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
  std::array<uint32_t, hours> count{}, errors{}, cursor{};
};
struct Worker {
  Index index;
  std::vector<Endpoint> endpoints;
  std::vector<std::pair<uint32_t, uint32_t>> records;
};
int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "/data",
                out = argc > 2 ? argv[2] : "/out";
    Mapping input(data + "/requests.log");
    const size_t workers = input.size > 1000000 ? 6 : 1;
    std::vector<const char *> boundary(workers + 1, input.data + input.size);
    boundary[0] = input.data;
    for (size_t i = 1; i < workers; ++i) {
      const char *p = input.data + input.size * i / workers;
      while (p < input.data + input.size && *p++ != '\n') {
      }
      boundary[i] = p;
    }
    std::vector<Worker> local(workers);
    // Parse once into compact (latency, local group) records. After assigning
    // global ranges, each worker scatters its records without hashing again.
    auto scan = [&](size_t thread) {
      auto &w = local[thread];
      w.records.reserve((boundary[thread + 1] - boundary[thread]) / 64);
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
        auto &group = w.endpoints[id];
        bool error = p[1] == '5';
        p += 5;
        uint32_t latency = number(p);
        ++p;
        ++group.count[hour];
        group.errors[hour] += error;
        w.records.emplace_back(latency, id * hours + hour);
      }
    };
    std::vector<uint32_t> values;
    parallel(workers, scan);
    // PROFILE_PARSE_DONE
    std::vector<std::string_view> names;
    for (auto &w : local)
      for (auto &e : w.endpoints)
        names.push_back(e.name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    // Resolve endpoint IDs once, rather than hashing the same name for all 72
    // hours.
    std::vector<std::array<Endpoint *, 6>> references(names.size());
    for (size_t e = 0; e < names.size(); ++e)
      for (size_t t = 0; t < workers; ++t) {
        auto id = local[t].index.get(names[e], 0, false).first;
        if (id != UINT32_MAX)
          references[e][t] = &local[t].endpoints[id];
      }
    const size_t groups = names.size() * hours;
    std::vector<uint32_t> offsets(groups + 1), errors(groups);
    uint32_t total = 0;
    for (size_t h = 0; h < hours; ++h)
      for (size_t e = 0; e < names.size(); ++e) {
        size_t g = h * names.size() + e;
        offsets[g] = total;
        for (size_t t = 0; t < workers; ++t) {
          if (!references[e][t])
            continue;
          auto &endpoint = *references[e][t];
          endpoint.cursor[h] = total;
          total += endpoint.count[h];
          errors[g] += endpoint.errors[h];
        }
      }
    offsets[groups] = total;
    values.resize(total);
    // PROFILE_MERGE_DONE
    parallel(workers, [&](size_t t) {
      auto &w = local[t];
      for (auto [latency, group] : w.records)
        values[w.endpoints[group / hours].cursor[group % hours]++] = latency;
    });
    std::vector<Worker>().swap(local);
    // PROFILE_SCATTER_DONE
    std::vector<std::string> output(hours);
    std::atomic<size_t> next{0};
    parallel(workers, [&](size_t) {
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
          size_t g = h * names.size() + e;
          uint32_t n = offsets[g + 1] - offsets[g];
          if (!n)
            continue;
          auto first = values.begin() + offsets[g], last = first + n;
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
          append(text, errors[g]);
          text += ' ';
          append(text, uint64_t(errors[g]) * 1000000 / n);
          text += '\n';
        }
      }
    });
    // PROFILE_FORMAT_DONE
    std::filesystem::create_directories(out);
    std::string joined;
    joined.reserve(11000000);
    for (auto &part : output)
      joined += part;
    write_file(out + "/summary.txt", joined);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
