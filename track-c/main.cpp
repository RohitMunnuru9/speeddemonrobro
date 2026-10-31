#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct MappedFile {
  const char *data; size_t size;
  explicit MappedFile(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open records.csv");
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size == 0) { close(fd); throw std::runtime_error("empty or unreadable CSV"); }
    size = st.st_size;
    void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) throw std::runtime_error("cannot map records.csv");
    data = static_cast<const char *>(mapped);
  }
  ~MappedFile() { munmap(const_cast<char *>(data), size); }
};

struct Country {
  uint64_t count = 0, ids = 0, name_bytes = 0, note_bytes = 0;
  int64_t sum = 0, lo = INT64_MAX, hi = INT64_MIN;
  uint64_t names = 0;
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
    // The spec has 240 countries plus unmatched. Store every exact name once,
    // then use one bit per country to count distinct (name, country) pairs.
    if (codes.size() > 256) throw std::runtime_error("country limit exceeded");
    std::unordered_map<std::string, std::array<uint64_t, 4>> names;
    names.reserve(110000);
    MappedFile bytes(data + "/records.csv");
    const char *p = bytes.data, *end = p + bytes.size;
    auto number = [&]() { uint64_t n = 0; while (p < end && *p >= '0' && *p <= '9') n = n * 10 + *p++ - '0'; return n; };
    auto field = [&](bool retain) {
      std::string value;
      size_t length = 0;
      if (p < end && *p == '"') {
        ++p;
        while (p < end) {
          if (*p == '"') { ++p; if (p == end || *p != '"') break; }
          if (retain) value += *p;
          ++p; ++length;
        }
      } else {
        while (p < end && *p != ',' && *p != '\r' && *p != '\n') { if (retain) value += *p; ++p; ++length; }
      }
      return std::pair{std::move(value), length};
    };
    // The header contains no quoted newline; BOMs are skipped only at record starts.
    while (p < end && *p++ != '\n') {}
    while (p < end) {
      if (end - p >= 3 && static_cast<unsigned char>(*p) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF) p += 3;
      uint64_t id = number(); ++p;
      auto [name, name_length] = field(true); ++p;
      auto [unused_note, note_length] = field(false); ++p;
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
      g.name_bytes += name_length; g.note_bytes += note_length;
      auto &membership = names.try_emplace(std::move(name), std::array<uint64_t, 4>{}).first->second;
      const uint64_t bit = uint64_t{1} << (group % 64);
      auto &word = membership[group / 64];
      if (!(word & bit)) { word |= bit; ++g.names; }
    }
    std::map<std::string, size_t> order;
    for (size_t i = 0; i < codes.size(); ++i) order.emplace(codes[i], i);
    std::filesystem::create_directories(out);
    std::ofstream result(out + "/aggregates.txt", std::ios::binary);
    if (!result) throw std::runtime_error("cannot open output");
    for (const auto &[code, i] : order) {
      const auto &g = groups[i]; if (!g.count) continue;
      result << code << ' ' << g.count << ' ' << g.ids << ' ' << money(g.sum) << ' ' << money(g.lo) << ' ' << money(g.hi) << ' ' << g.names << ' ' << g.name_bytes << ' ' << g.note_bytes << '\n';
    }
    if (!result) throw std::runtime_error("output write failed");
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
