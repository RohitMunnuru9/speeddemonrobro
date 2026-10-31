#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Group { std::vector<uint32_t> values; uint32_t errors = 0; };
int main(int argc, char **argv) {
  try {
    const std::string data = argc > 1 ? argv[1] : "/data";
    const std::string out = argc > 2 ? argv[2] : "/out";
    std::ifstream f(data + "/requests.log", std::ios::binary);
    if (!f) throw std::runtime_error("cannot open requests.log");
    std::unordered_map<std::string, std::array<Group, 72>> groups;
    groups.reserve(2048);
    std::string line;
    while (std::getline(f, line)) {
      // Only the date and hour affect the key; fractional seconds are irrelevant.
      const int day = line[6] == '0' ? (line[9] == '0' ? 0 : 1) : line[9] - '0' + 1;
      const int hour = day * 24 + (line[11] - '0') * 10 + line[12] - '0' - 18;
      const auto begin = line.find(' ', 28) + 1;
      const auto end = line.find(' ', begin);
      auto &g = groups[line.substr(begin, end - begin)][hour];
      g.errors += line[end + 1] == '5';
      uint32_t latency = 0;
      for (size_t p = end + 5; p < line.size(); ++p) latency = latency * 10 + line[p] - '0';
      g.values.push_back(latency);
    }
    std::vector<std::string> endpoints;
    for (const auto &[key, unused] : groups) endpoints.push_back(key);
    std::sort(endpoints.begin(), endpoints.end());
    std::filesystem::create_directories(out);
    std::ofstream result(out + "/summary.txt", std::ios::binary);
    if (!result) throw std::runtime_error("cannot open output");
    for (int h = 0; h < 72; ++h) {
      const int absolute = h + 18, day = absolute / 24, hour = absolute % 24;
      char timestamp[32];
      std::snprintf(timestamp, sizeof(timestamp), "2026-%02d-%02dT%02d:00:00Z", day < 2 ? 10 : 11, day < 2 ? 30 + day : day - 1, hour);
      for (const auto &endpoint : endpoints) {
        auto &g = groups.at(endpoint)[h];
        auto &v = g.values;
        if (v.empty()) continue;
        std::sort(v.begin(), v.end());
        const uint64_t n = v.size();
        result << timestamp << ' ' << endpoint << ' ' << n;
        for (int p : {50, 95, 99}) result << ' ' << v[(p * n + 99) / 100 - 1];
        result << ' ' << g.errors << ' ' << uint64_t(g.errors) * 1000000 / n << '\n';
      }
    }
    if (!result) throw std::runtime_error("output write failed");
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
