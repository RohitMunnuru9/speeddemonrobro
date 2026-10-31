#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

struct Country {
  uint64_t count = 0, ids = 0, name_bytes = 0, note_bytes = 0;
  int64_t sum = 0, lo = INT64_MAX, hi = INT64_MIN;
  std::unordered_set<std::string> names;
};
std::string money(int64_t n) {
  std::string sign = n < 0 ? "-" : "";
  uint64_t a = n < 0 ? -n : n;
  return sign + std::to_string(a / 100) + "." + char('0' + a / 10 % 10) + char('0' + a % 10);
}
int main(int argc, char **argv) {
  try {
    const std::string data = argc > 1 ? argv[1] : "/data", out = argc > 2 ? argv[2] : "/out";
    std::ifstream lookup(data + "/regions.csv", std::ios::binary);
    if (!lookup) throw std::runtime_error("cannot open regions.csv");
    std::vector<std::string> codes;
    std::map<std::string, uint16_t> country_ids;
    std::vector<uint16_t> regions(1000000, UINT16_MAX);
    std::string line;
    std::getline(lookup, line);
    while (std::getline(lookup, line)) {
      auto comma = line.find(',');
      uint32_t id = std::stoul(line.substr(0, comma));
      std::string code = line.substr(comma + 1);
      auto [it, added] = country_ids.emplace(code, codes.size());
      if (added) codes.push_back(code);
      if (id >= regions.size()) regions.resize(id + 1, UINT16_MAX);
      regions[id] = it->second;
    }
    const size_t unmatched = codes.size();
    codes.push_back("_UNMATCHED");
    std::vector<Country> groups(codes.size());
    std::ifstream f(data + "/records.csv", std::ios::binary);
    if (!f) throw std::runtime_error("cannot open records.csv");
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    const char *p = bytes.data(), *end = p + bytes.size();
    auto number = [&]() { uint64_t n = 0; while (p < end && *p >= '0' && *p <= '9') n = n * 10 + *p++ - '0'; return n; };
    auto field = [&]() {
      std::string value;
      if (p < end && *p == '"') {
        ++p;
        while (p < end) {
          if (*p == '"') { ++p; if (p == end || *p != '"') break; }
          value += *p++;
        }
      } else {
        while (p < end && *p != ',' && *p != '\r' && *p != '\n') value += *p++;
      }
      return value;
    };
    // The header contains no quoted newline; BOMs are skipped only at record starts.
    while (p < end && *p++ != '\n') {}
    while (p < end) {
      if (end - p >= 3 && static_cast<unsigned char>(*p) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF) p += 3;
      uint64_t id = number(); ++p;
      std::string name = field(); ++p;
      std::string note = field(); ++p;
      uint64_t region = number(); ++p;
      bool negative = p < end && *p == '-'; if (negative) ++p;
      int64_t cents = number() * 100; ++p;
      cents += number(); if (negative) cents = -cents;
      if (p < end && *p == '\r') ++p;
      if (p < end && *p == '\n') ++p;
      size_t group = region < regions.size() && regions[region] != UINT16_MAX ? regions[region] : unmatched;
      auto &g = groups[group];
      ++g.count; g.ids += id; g.sum += cents;
      g.lo = std::min(g.lo, cents); g.hi = std::max(g.hi, cents);
      g.name_bytes += name.size(); g.note_bytes += note.size();
      g.names.insert(std::move(name));
    }
    std::map<std::string, size_t> order;
    for (size_t i = 0; i < codes.size(); ++i) order.emplace(codes[i], i);
    std::filesystem::create_directories(out);
    std::ofstream result(out + "/aggregates.txt", std::ios::binary);
    if (!result) throw std::runtime_error("cannot open output");
    for (const auto &[code, i] : order) {
      const auto &g = groups[i]; if (!g.count) continue;
      result << code << ' ' << g.count << ' ' << g.ids << ' ' << money(g.sum) << ' ' << money(g.lo) << ' ' << money(g.hi) << ' ' << g.names.size() << ' ' << g.name_bytes << ' ' << g.note_bytes << '\n';
    }
    if (!result) throw std::runtime_error("output write failed");
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
