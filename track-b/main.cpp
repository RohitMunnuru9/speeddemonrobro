#include "io.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <filesystem>
#include <immintrin.h>
#include <iostream>
#include <numeric>
#include <string_view>
#include <unordered_map>

using IDs = std::vector<uint32_t>;
bool word_byte(unsigned char c) {
  return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
}
template <class F>
__attribute__((target("avx2"))) void tokenize(std::string_view text,
                                              F consume) {
  const char *p = text.data(), *end = p + text.size();
  while (p < end) {
    while (p < end && !word_byte(*p))
      ++p;
    if (p == end)
      break;
    if (end - p >= 32) {
      auto raw = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
      auto lower = _mm256_or_si256(raw, _mm256_set1_epi8(32));
      auto letters =
          _mm256_and_si256(_mm256_cmpgt_epi8(lower, _mm256_set1_epi8('a' - 1)),
                           _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), lower));
      auto digits =
          _mm256_and_si256(_mm256_cmpgt_epi8(raw, _mm256_set1_epi8('0' - 1)),
                           _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), raw));
      uint32_t separators =
          ~uint32_t(_mm256_movemask_epi8(_mm256_or_si256(letters, digits)));
      if (separators) {
        size_t size = __builtin_ctz(separators);
        char bytes[32];
        const auto positions = _mm256_setr_epi8(
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
            19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
        auto keep = _mm256_cmpgt_epi8(_mm256_set1_epi8(size), positions);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(bytes),
                            _mm256_and_si256(lower, keep));
        p += size;
        if (!consume(std::string_view(bytes, size)))
          return;
        continue;
      }
    }
    char bytes[32]{};
    size_t size = 0;
    while (p < end && word_byte(*p)) {
      if (size == sizeof(bytes))
        throw std::runtime_error("token exceeds spec bound");
      unsigned char c = *p++;
      bytes[size++] = c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    if (!consume(std::string_view(bytes, size)))
      return;
  }
}
struct Query {
  bool phrase;
  IDs terms, prefix;
  std::vector<char> ops;
};
struct Key {
  uint64_t a = 0, b = 0, c = 0, d = 0;
  bool operator==(const Key &) const = default;
};
uint64_t key_hash(Key k) {
  uint64_t h =
      k.a ^ std::rotl(k.b, 17) ^ std::rotl(k.c, 31) ^ std::rotl(k.d, 47);
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return h ? h : 1;
}
struct TermSlot {
  uint64_t hash = 0;
  uint32_t id = 0;
};
std::string format(const IDs &ids) {
  std::string out;
  append(out, ids.size());
  out += ' ';
  append(out, std::accumulate(ids.begin(), ids.end(), uint64_t{0}));
  for (size_t i = 0; i < std::min<size_t>(10, ids.size()); ++i) {
    out += ' ';
    append(out, ids[i]);
  }
  out += '\n';
  return out;
}
std::string format_bits(const std::vector<uint64_t> &bits) {
  uint64_t count = 0, sum = 0;
  IDs first;
  for (size_t i = 0; i < bits.size(); ++i) {
    uint64_t w = bits[i];
    uint64_t n = std::popcount(w);
    count += n;
    sum += i * 64 * n + std::popcount(w & 0xAAAAAAAAAAAAAAAAULL) +
           2 * std::popcount(w & 0xCCCCCCCCCCCCCCCCULL) +
           4 * std::popcount(w & 0xF0F0F0F0F0F0F0F0ULL) +
           8 * std::popcount(w & 0xFF00FF00FF00FF00ULL) +
           16 * std::popcount(w & 0xFFFF0000FFFF0000ULL) +
           32 * std::popcount(w & 0xFFFFFFFF00000000ULL);
    while (w && first.size() < 10) {
      first.push_back(i * 64 + std::countr_zero(w));
      w &= w - 1;
    }
  }
  std::string out;
  append(out, count);
  out += ' ';
  append(out, sum);
  for (auto id : first) {
    out += ' ';
    append(out, id);
  }
  out += '\n';
  return out;
}
int main(int argc, char **argv) {
  try {
    const std::string data = argc > 1 ? argv[1] : "/data",
                      out = argc > 2 ? argv[2] : "/out";
    Mapping corpus(data + "/docs.txt"), query_file(data + "/queries.txt");
    std::unordered_map<std::string_view, uint32_t> dictionary, unique_queries;
    std::vector<std::string_view> words;
    std::vector<Query> queries;
    IDs order;
    const char *p = query_file.data, *end = p + query_file.size;
    auto intern = [&](std::string_view word) {
      auto [it, added] = dictionary.emplace(word, words.size());
      if (added)
        words.push_back(word);
      return it->second;
    };
    while (p < end) {
      const char *newline = static_cast<const char *>(memchr(p, '\n', end - p));
      std::string_view line(p, newline - p);
      p = newline + 1;
      auto [it, added] = unique_queries.emplace(line, queries.size());
      order.push_back(it->second);
      if (!added)
        continue;
      Query q;
      q.phrase = line[0] == 'P';
      auto rest = q.phrase ? line.substr(8, line.size() - 9) : line.substr(5);
      bool term = true;
      while (!rest.empty()) {
        size_t space = rest.find(' ');
        auto word = rest.substr(0, space);
        if (q.phrase || term)
          q.terms.push_back(intern(word));
        else
          q.ops.push_back(word[0]);
        term = !term;
        if (space == std::string_view::npos)
          break;
        rest.remove_prefix(space + 1);
      }
      if (q.phrase) {
        q.prefix.resize(q.terms.size());
        for (size_t i = 1, j = 0; i < q.terms.size(); ++i) {
          while (j && q.terms[i] != q.terms[j])
            j = q.prefix[j - 1];
          if (q.terms[i] == q.terms[j])
            ++j;
          q.prefix[i] = j;
        }
      }
      queries.push_back(std::move(q));
    }
    size_t capacity = 1;
    while (capacity < words.size() * 2)
      capacity *= 2;
    std::vector<TermSlot> table(capacity);
    std::vector<Key> keys(words.size());
    for (uint32_t id = 0; id < words.size(); ++id) {
      memcpy(&keys[id], words[id].data(), words[id].size());
      uint64_t hash = key_hash(keys[id]);
      size_t slot = hash & (capacity - 1);
      while (table[slot].hash)
        slot = (slot + 1) & (capacity - 1);
      table[slot] = {hash, id};
    }
    std::unordered_map<std::string_view, uint32_t>().swap(dictionary);
    auto lookup_term = [&](std::string_view token) {
      // tokenize supplies a fully initialized, zero-padded 32-byte key. ASCII
      // alphanumerics contain no zero, so padding preserves exact token
      // identity.
      Key key;
      memcpy(&key, token.data(), sizeof(key));
      uint64_t hash = key_hash(key);
      size_t slot = hash & (capacity - 1);
      while (table[slot].hash &&
             (table[slot].hash != hash || keys[table[slot].id] != key))
        slot = (slot + 1) & (capacity - 1);
      return table[slot].hash ? table[slot].id : UINT32_MAX;
    };
    const size_t documents =
        std::count(corpus.data, corpus.data + corpus.size, '\n');
    std::vector<std::string_view> texts(documents);
    // Sample current input to choose representations before building postings.
    // The choice changes storage only; no term or document is omitted.
    IDs sample_frequency(words.size()), sample_last(words.size(), UINT32_MAX);
    size_t sample_count = std::min<size_t>(4096, documents);
    const char *sample = corpus.data;
    for (size_t i = 0; i < sample_count; ++i) {
      uint32_t id = number(sample);
      ++sample;
      const char *newline = static_cast<const char *>(
          memchr(sample, '\n', corpus.data + corpus.size - sample));
      tokenize(std::string_view(sample, newline - sample),
               [&](std::string_view token) {
                 uint32_t term = lookup_term(token);
                 if (term != UINT32_MAX && sample_last[term] != id) {
                   sample_last[term] = id;
                   ++sample_frequency[term];
                 }
                 return true;
               });
      sample = newline + 1;
    }
    std::vector<std::vector<uint64_t>> bitmaps(words.size());
    for (size_t term = 0; term < words.size(); ++term)
      if (sample_frequency[term] > sample_count / 64)
        bitmaps[term].resize((documents + 63) / 64);
    const size_t workers = corpus.size > 1000000 ? 6 : 1;
    std::vector<const char *> boundaries(workers + 1,
                                         corpus.data + corpus.size);
    boundaries[0] = corpus.data;
    for (size_t t = 1; t < workers; ++t) {
      const char *q = corpus.data + corpus.size * t / workers;
      while (q < corpus.data + corpus.size && *q++ != '\n') {
      }
      // Each worker owns whole bitmap words, so writes need no atomics.
      while (q < corpus.data + corpus.size) {
        const char *id_pointer = q;
        uint32_t id = number(id_pointer);
        if (id % 64 == 0)
          break;
        q = static_cast<const char *>(
                memchr(q, '\n', corpus.data + corpus.size - q)) +
            1;
      }
      boundaries[t] = q;
    }
    std::vector<std::vector<IDs>> local(workers,
                                        std::vector<IDs>(words.size()));
    parallel(workers, [&](size_t t) {
      IDs last(words.size(), UINT32_MAX);
      const char *q = boundaries[t];
      while (q < boundaries[t + 1]) {
        uint32_t id = number(q);
        ++q;
        const char *newline =
            static_cast<const char *>(memchr(q, '\n', boundaries[t + 1] - q));
        texts[id] = {q, size_t(newline - q)};
        q = newline + 1;
        tokenize(texts[id], [&](std::string_view token) {
          uint32_t term = lookup_term(token);
          if (term != UINT32_MAX) {
            if (!bitmaps[term].empty())
              bitmaps[term][id / 64] |= uint64_t{1} << (id % 64);
            else if (last[term] != id) {
              last[term] = id;
              local[t][term].push_back(id);
            }
          }
          return true;
        });
      }
    });
    std::vector<IDs> postings(words.size());
    std::atomic<size_t> next{0};
    parallel(workers, [&](size_t) {
      for (;;) {
        size_t term = next.fetch_add(1);
        if (term >= words.size())
          break;
        size_t count = 0;
        for (auto &worker : local)
          count += worker[term].size();
        auto &target = postings[term];
        target.reserve(count);
        for (auto &worker : local) {
          target.insert(target.end(), worker[term].begin(), worker[term].end());
          IDs().swap(worker[term]);
        }
      }
    });
    std::vector<std::vector<IDs>>().swap(local);
    IDs frequencies(words.size());
    next = 0;
    parallel(workers, [&](size_t) {
      for (;;) {
        size_t term = next.fetch_add(1);
        if (term >= words.size())
          break;
        frequencies[term] = postings[term].size();
        for (auto w : bitmaps[term])
          frequencies[term] += std::popcount(w);
      }
    });
    std::vector<std::string> output(queries.size());
    next = 0;
    parallel(workers, [&](size_t) {
      IDs a, b;
      std::vector<uint64_t> bits;
      for (;;) {
        size_t i = next.fetch_add(1);
        if (i >= queries.size())
          break;
        const auto &q = queries[i];
        if (!q.phrase) {
          if (q.ops.empty()) {
            output[i] = bitmaps[q.terms[0]].empty()
                            ? format(postings[q.terms[0]])
                            : format_bits(bitmaps[q.terms[0]]);
            continue;
          }
          bool dense = !bitmaps[q.terms[0]].empty();
          if (dense)
            bits = bitmaps[q.terms[0]];
          else
            a.assign(postings[q.terms[0]].begin(), postings[q.terms[0]].end());
          for (size_t k = 0; k < q.ops.size(); ++k) {
            const auto &right_bits = bitmaps[q.terms[k + 1]];
            const auto &right = postings[q.terms[k + 1]];
            char op = q.ops[k];
            if (!right_bits.empty()) {
              if (dense) {
                for (size_t w = 0; w < bits.size(); ++w) {
                  if (op == 'A')
                    bits[w] &= right_bits[w];
                  else if (op == 'O')
                    bits[w] |= right_bits[w];
                  else
                    bits[w] &= ~right_bits[w];
                }
              } else if (op == 'O') {
                bits = right_bits;
                for (auto id : a)
                  bits[id / 64] |= uint64_t{1} << (id % 64);
                dense = true;
              } else {
                b.clear();
                for (auto id : a)
                  if (bool(right_bits[id / 64] & (uint64_t{1} << (id % 64))) ==
                      (op == 'A'))
                    b.push_back(id);
                a.swap(b);
              }
              continue;
            }
            if (dense) {
              if (op == 'A') {
                b.clear();
                for (auto id : right)
                  if (bits[id / 64] & (uint64_t{1} << (id % 64)))
                    b.push_back(id);
                a.swap(b);
                dense = false;
              } else
                for (auto id : right) {
                  if (op == 'O')
                    bits[id / 64] |= uint64_t{1} << (id % 64);
                  else
                    bits[id / 64] &= ~(uint64_t{1} << (id % 64));
                }
              continue;
            }
            b.clear();
            b.reserve(q.ops[k] == 'O' ? a.size() + right.size() : a.size());
            if (q.ops[k] == 'A')
              std::set_intersection(a.begin(), a.end(), right.begin(),
                                    right.end(), std::back_inserter(b));
            else if (q.ops[k] == 'O')
              std::set_union(a.begin(), a.end(), right.begin(), right.end(),
                             std::back_inserter(b));
            else
              std::set_difference(a.begin(), a.end(), right.begin(),
                                  right.end(), std::back_inserter(b));
            a.swap(b);
          }
          if (dense) {
            output[i] = format_bits(bits);
            continue;
          }
        } else {
          a.clear();
          uint32_t anchor = *std::min_element(
              q.terms.begin(), q.terms.end(),
              [&](auto x, auto y) { return frequencies[x] < frequencies[y]; });
          auto candidate_doc = [&](uint32_t doc) {
            bool candidate = true;
            for (auto term : q.terms)
              if (term != anchor) {
                bool contains =
                    bitmaps[term].empty()
                        ? std::binary_search(postings[term].begin(),
                                             postings[term].end(), doc)
                        : bool(bitmaps[term][doc / 64] &
                               (uint64_t{1} << (doc % 64)));
                if (!contains) {
                  candidate = false;
                  break;
                }
              }
            if (!candidate)
              return;
            size_t matched = 0;
            bool found = false;
            tokenize(texts[doc], [&](std::string_view word) {
              Key key;
              memcpy(&key, word.data(), sizeof(key));
              while (matched && key != keys[q.terms[matched]])
                matched = q.prefix[matched - 1];
              if (key == keys[q.terms[matched]])
                ++matched;
              if (matched == q.terms.size()) {
                found = true;
                return false;
              }
              return true;
            });
            if (found)
              a.push_back(doc);
          };
          if (bitmaps[anchor].empty())
            for (uint32_t doc : postings[anchor])
              candidate_doc(doc);
          else
            for (size_t w = 0; w < bitmaps[anchor].size(); ++w) {
              uint64_t bits = bitmaps[anchor][w];
              while (bits) {
                candidate_doc(w * 64 + std::countr_zero(bits));
                bits &= bits - 1;
              }
            }
        }
        output[i] = format(a);
      }
    });
    std::string joined;
    joined.reserve(order.size() * 32);
    for (auto i : order)
      joined += output[i];
    std::filesystem::create_directories(out);
    write_file(out + "/results.txt", joined);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
