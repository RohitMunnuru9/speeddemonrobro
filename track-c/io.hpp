#pragma once
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
struct Mapping {
  const char *data = ""; size_t size = 0;
  explicit Mapping(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("open: " + path);
    struct stat st{};
    if (fstat(fd, &st)) { close(fd); throw std::runtime_error("fstat failed"); }
    size = st.st_size;
    if (size) {
      void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0); close(fd);
      if (p == MAP_FAILED) throw std::runtime_error("mmap failed");
      data = static_cast<const char *>(p);
    } else close(fd);
  }
  ~Mapping() { if (size) munmap(const_cast<char *>(data), size); }
};
template<class F> void parallel(size_t n, F function) {
  std::vector<std::thread> threads; std::vector<std::exception_ptr> errors(n);
  for (size_t i = 0; i < n; ++i) threads.emplace_back([&, i] {
    try { function(i); } catch (...) { errors[i] = std::current_exception(); }
  });
  for (auto &thread : threads) thread.join();
  for (auto error : errors) if (error) std::rethrow_exception(error);
}
inline uint64_t number(const char *&p) {
  uint64_t v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + *p++ - '0';
  return v;
}
inline void append(std::string &s, uint64_t value) {
  char bytes[24]; auto result = std::to_chars(bytes, bytes + sizeof(bytes), value); s.append(bytes, result.ptr);
}
inline void write_file(const std::string &path, const std::string &bytes) {
  FILE *f = fopen(path.c_str(), "wb"); if (!f) throw std::runtime_error("open output failed");
  bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
  if (fclose(f) || !ok) throw std::runtime_error("write output failed");
}
