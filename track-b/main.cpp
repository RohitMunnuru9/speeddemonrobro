#include "io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <immintrin.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <span>
#include <string_view>
#include <unordered_map>

using IDs = std::vector<uint32_t>;
using ShortIDs = std::vector<uint16_t>;
struct PostingSpan {
  uint32_t offset = 0;
  uint16_t count = 0, capacity = 0;
};
struct PostingBuffer {
  ShortIDs values;
  std::vector<ShortIDs> overflow;
};
struct PostingStore {
  const std::vector<PostingBuffer> *buffers;
  const std::vector<PostingSpan> *spans;
  const IDs *starts;
  size_t terms, workers;
  std::span<const uint16_t> range(size_t worker, size_t term) const {
    const auto &span = (*spans)[worker * terms + term];
    if (!span.count)
      return {};
    const auto &buffer = (*buffers)[worker];
    return span.capacity
               ? std::span<const uint16_t>(buffer.values.data() + span.offset,
                                           span.count)
               : std::span<const uint16_t>(buffer.overflow[span.offset]);
  }
};
struct PostingView {
  const PostingStore *store = nullptr;
  size_t term = 0, count = 0;
  struct Iterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = uint32_t;
    using difference_type = std::ptrdiff_t;
    using reference = uint32_t;
    using pointer = void;
    const PostingView *view;
    size_t worker = 0, position = 0;
    void skip() {
      while (worker < view->store->workers) {
        if (position < view->store->range(worker, view->term).size())
          break;
        ++worker;
        position = 0;
      }
    }
    uint32_t operator*() const {
      return (*view->store->starts)[worker] +
             view->store->range(worker, view->term)[position];
    }
    Iterator &operator++() {
      ++position;
      skip();
      return *this;
    }
    Iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    bool operator==(const Iterator &) const = default;
  };
  Iterator begin() const {
    Iterator i{this};
    i.skip();
    return i;
  }
  Iterator end() const { return {this, store->workers, 0}; }
  size_t size() const { return count; }
  bool contains(uint32_t doc) const {
    size_t worker =
        std::upper_bound(store->starts->begin(), store->starts->end(), doc) -
        store->starts->begin() - 1;
    auto values = store->range(worker, term);
    return std::binary_search(values.begin(), values.end(),
                              doc - (*store->starts)[worker]);
  }
};
struct Key {
  uint64_t a = 0, b = 0, c = 0, d = 0;
  bool operator==(const Key &) const = default;
};
bool word_byte(unsigned char c) {
  return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
}

template <class F>
__attribute__((target("avx2"))) void tokenize(std::string_view text,
                                              F consume) {
  if (text.empty())
    return;
  // Only the final 64 input bytes need padding. Both classification and key
  // loads can then cross the text boundary without crossing an allocation.
  alignas(32) char tail[128]{};
  size_t tail_start = text.size() > 64 ? text.size() - 64 : 0;
  memcpy(tail, text.data() + tail_start, text.size() - tail_start);
  auto folded_mask = [](__m256i raw) {
    auto folded = _mm256_or_si256(raw, _mm256_set1_epi8(32));
    auto letters =
        _mm256_and_si256(_mm256_cmpgt_epi8(folded, _mm256_set1_epi8('a' - 1)),
                         _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), folded));
    auto digits =
        _mm256_and_si256(_mm256_cmpgt_epi8(raw, _mm256_set1_epi8('0' - 1)),
                         _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), raw));
    return uint32_t(_mm256_movemask_epi8(_mm256_or_si256(letters, digits)));
  };
  const auto positions = _mm256_setr_epi8(
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
      21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
  auto emit = [&](size_t first, size_t last) {
    size_t size = last - first;
    if (size > 32)
      throw std::runtime_error("token exceeds spec bound");
    const char *p = first + 32 <= text.size() ? text.data() + first
                                              : tail + first - tail_start;
    auto raw = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
    auto keep = _mm256_cmpgt_epi8(_mm256_set1_epi8(size), positions);
    Key word;
    _mm256_storeu_si256(
        reinterpret_cast<__m256i *>(&word),
        _mm256_and_si256(_mm256_or_si256(raw, _mm256_set1_epi8(32)), keep));
    if constexpr (std::is_invocable_r_v<bool, F, const Key &, size_t>)
      return consume(word, first);
    else
      return consume(word);
  };
  size_t pending = 0;
  uint64_t carry = 0;
  for (size_t base = 0; base < text.size(); base += 64) {
    const char *p = base + 64 <= text.size() ? text.data() + base
                                             : tail + base - tail_start;
    uint64_t mask =
        folded_mask(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(p))) |
        (uint64_t(folded_mask(
             _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 32))))
         << 32);
    uint64_t shifted = (mask << 1) | carry, starts = mask & ~shifted,
             ends = ~mask & shifted;
    if (carry && ends) {
      if (!emit(pending, base + std::countr_zero(ends)))
        return;
      ends &= ends - 1;
    }
    while (ends) {
      size_t first = base + std::countr_zero(starts),
             last = base + std::countr_zero(ends);
      if (!emit(first, last))
        return;
      starts &= starts - 1;
      ends &= ends - 1;
    }
    if (starts)
      pending = base + std::countr_zero(starts);
    carry = mask >> 63;
  }
  if (carry)
    emit(pending, text.size());
}
struct Query {
  bool phrase;
  IDs terms, prefix;
  std::vector<char> ops;
};
uint64_t key_hash(const Key &k) {
  uint64_t h =
      k.a ^ std::rotl(k.b, 17) ^ std::rotl(k.c, 31) ^ std::rotl(k.d, 47);
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return h ? h : 1;
}
struct TermSlot {
  uint64_t prefix = 0;
  uint32_t id = 0;
  uint32_t long_key = 0;
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
std::string format(const PostingView &ids) {
  std::string out;
  append(out, ids.size());
  out += ' ';
  append(out, std::accumulate(ids.begin(), ids.end(), uint64_t{0}));
  auto it = ids.begin();
  for (size_t i = 0; i < std::min<size_t>(10, ids.size()); ++i, ++it) {
    out += ' ';
    append(out, *it);
  }
  out += '\n';
  return out;
}
__attribute__((target("avx2"))) std::string
format_bits(const std::vector<uint64_t> &bits) {
  const auto zero = _mm256_setzero_si256(), nibble = _mm256_set1_epi8(15);
  const auto pop =
      _mm256_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1,
                       2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
  const auto weight =
      _mm256_setr_epi8(0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 0, 0, 1,
                       1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6);
  const auto byte_positions = _mm256_setr_epi8(
      0, 8, 16, 24, 32, 40, 48, 56, 0, 8, 16, 24, 32, 40, 48, 56, 0, 8, 16, 24,
      32, 40, 48, 56, 0, 8, 16, 24, 32, 40, 48, 56);
  auto bases = _mm256_setr_epi64x(0, 64, 128, 192);
  auto counts = zero, sums = zero;
  size_t i = 0;
  for (; i + 4 <= bits.size(); i += 4) {
    auto words =
        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(bits.data() + i));
    auto current_bases = bases;
    bases = _mm256_add_epi64(bases, _mm256_set1_epi64x(256));
    if (_mm256_testz_si256(words, words))
      continue;
    auto low = _mm256_and_si256(words, nibble),
         high = _mm256_and_si256(_mm256_srli_epi16(words, 4), nibble);
    auto lower_count = _mm256_shuffle_epi8(pop, low),
         upper_count = _mm256_shuffle_epi8(pop, high);
    auto byte_count = _mm256_add_epi8(lower_count, upper_count);
    auto within =
        _mm256_add_epi8(_mm256_add_epi8(_mm256_shuffle_epi8(weight, low),
                                        _mm256_shuffle_epi8(weight, high)),
                        _mm256_slli_epi16(upper_count, 2));
    auto product = _mm256_madd_epi16(
        _mm256_maddubs_epi16(byte_positions, byte_count), _mm256_set1_epi16(1));
    auto paired = _mm256_hadd_epi32(product, zero);
    auto byte_sum = _mm256_shuffle_epi32(paired, _MM_SHUFFLE(3, 1, 2, 0));
    auto word_count = _mm256_sad_epu8(byte_count, zero);
    counts = _mm256_add_epi64(counts, word_count);
    sums = _mm256_add_epi64(
        sums, _mm256_add_epi64(
                  _mm256_mul_epu32(word_count, current_bases),
                  _mm256_add_epi64(byte_sum, _mm256_sad_epu8(within, zero))));
  }
  uint64_t count_lanes[4], sum_lanes[4];
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(count_lanes), counts);
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(sum_lanes), sums);
  uint64_t count = 0, sum = 0;
  for (size_t lane = 0; lane < 4; ++lane) {
    count += count_lanes[lane];
    sum += sum_lanes[lane];
  }
  for (; i < bits.size(); ++i) {
    uint64_t w = bits[i];
    count += std::popcount(w);
    while (w) {
      sum += i * 64 + std::countr_zero(w);
      w &= w - 1;
    }
  }
  IDs first;
  for (size_t j = 0; j < bits.size() && first.size() < 10; ++j) {
    uint64_t w = bits[j];
    while (w && first.size() < 10) {
      first.push_back(j * 64 + std::countr_zero(w));
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
    std::array<uint64_t, 8192> bloom{};
    std::vector<Key> keys(words.size());
    for (uint32_t id = 0; id < words.size(); ++id) {
      memcpy(&keys[id], words[id].data(), words[id].size());
      uint64_t hash = key_hash(keys[id]);
      bloom[(hash >> 12) & (bloom.size() - 1)] |=
          (uint64_t{1} << (hash & 63)) | (uint64_t{1} << ((hash >> 6) & 63));
      size_t slot = hash & (capacity - 1);
      while (table[slot].prefix)
        slot = (slot + 1) & (capacity - 1);
      table[slot] = {keys[id].a, id, keys[id].b != 0};
    }
    std::unordered_map<std::string_view, uint32_t>().swap(dictionary);
    struct alignas(64) Hot {
      Key key;
      uint32_t id = 0;
    };
    std::array<Hot, 256> hot{};
    auto hot_slot = [](uint64_t prefix) {
      return (prefix * 0x9e3779b97f4a7c15ULL) >> 56;
    };
    auto lookup_prepared = [&](const Key &key, uint64_t hash) {
      uint64_t mask =
          (uint64_t{1} << (hash & 63)) | (uint64_t{1} << ((hash >> 6) & 63));
      if ((bloom[(hash >> 12) & (bloom.size() - 1)] & mask) != mask)
        return UINT32_MAX;
      size_t slot = hash & (capacity - 1);
      while (table[slot].prefix) {
        const auto &entry = table[slot];
        if (entry.prefix == key.a &&
            ((!key.b && !entry.long_key) || keys[entry.id] == key))
          return entry.id;
        slot = (slot + 1) & (capacity - 1);
      }
      return UINT32_MAX;
    };
    auto lookup_term = [&](const Key &key) {
      const auto &cached = hot[hot_slot(key.a)];
      return cached.key == key ? cached.id
                               : lookup_prepared(key, key_hash(key));
    };
    // PROFILE_PREPARED_QUERIES
    size_t documents = 0;
    if (corpus.size) {
      const char *last = corpus.data + corpus.size - 1;
      while (last > corpus.data && last[-1] != '\n')
        --last;
      documents = number(last) + 1;
    }
    std::vector<std::string_view> texts(documents);
    size_t scanners = corpus.size > 1000000 ? 12 : 1;
    parallel(scanners, [&](size_t t) {
      const char *end = corpus.data + corpus.size;
      auto boundary = [&](size_t part) {
        const char *p = corpus.data + corpus.size * part / scanners;
        if (part && part < scanners)
          while (p < end && *p++ != '\n') {
          }
        return p;
      };
      const char *line = boundary(t), *stop = boundary(t + 1);
      while (line < stop) {
        size_t id = number(line);
        ++line;
        const char *newline =
            static_cast<const char *>(memchr(line, '\n', stop - line));
        texts[id] = {line, size_t(newline - line)};
        line = newline + 1;
      }
    });
    // Sample current input to choose representations before building postings.
    // The choice changes storage only; no term or document is omitted.
    IDs sample_frequency(words.size()), sample_tokens(words.size()),
        sample_last(words.size(), UINT32_MAX);
    size_t sample_count = std::min<size_t>(4096, documents);
    for (size_t i = 0; i < sample_count; ++i) {
      uint32_t id = i;
      tokenize(texts[id], [&](const Key &token) {
        uint32_t term = lookup_term(token);
        if (term != UINT32_MAX)
          ++sample_tokens[term];
        if (term != UINT32_MAX && sample_last[term] != id) {
          sample_last[term] = id;
          ++sample_frequency[term];
        }
        return true;
      });
    }
    IDs position_ids(words.size(), UINT32_MAX);
    uint32_t position_terms = 0;
    for (const auto &q : queries)
      if (q.phrase) {
        auto anchor = *std::min_element(
            q.terms.begin(), q.terms.end(), [&](auto x, auto y) {
              return sample_frequency[x] < sample_frequency[y];
            });
        if (sample_frequency[anchor] <= 128 &&
            position_ids[anchor] == UINT32_MAX)
          position_ids[anchor] = position_terms++;
      }
    std::vector<std::vector<uint64_t>> bitmaps(words.size());
    for (uint32_t term = 0; term < words.size(); ++term)
      if (sample_tokens[term]) {
        auto &entry = hot[hot_slot(keys[term].a)];
        if (!entry.key.a || sample_tokens[term] > sample_tokens[entry.id])
          entry = {keys[term], term};
      }
    for (size_t term = 0; term < words.size(); ++term)
      if (sample_frequency[term] > sample_count / 64)
        bitmaps[term].resize((documents + 63) / 64);
    size_t workers = corpus.size > 1000000 ? 6 : 1;
    if (const char *env = std::getenv("SD_THREADS"))
      workers = std::clamp(std::atoi(env), 1, 12);
    workers = std::max(workers, (documents + 65471) / 65472);
    if (workers > 12)
      throw std::runtime_error("corpus exceeds spec scale");
    size_t span = ((documents + workers * 64 - 1) / (workers * 64)) * 64;
    if (span > UINT16_MAX)
      throw std::runtime_error("posting block exceeds spec scale");
    IDs starts(workers + 1);
    for (size_t t = 0; t <= workers; ++t)
      starts[t] = std::min(documents, span * t);
    // Each block owns whole bitmap words and stores sparse document IDs as
    // exact uint16 offsets. Views merge blocks lazily, avoiding a full copy of
    // postings.
    std::vector<PostingSpan> spans(workers * words.size());
    std::vector<PostingBuffer> buffers(workers);
    std::vector<std::vector<IDs>> positions(workers,
                                            std::vector<IDs>(position_terms));
    parallel(workers, [&](size_t t) {
      auto &buffer = buffers[t];
      size_t total = 0, block = starts[t + 1] - starts[t];
      for (size_t term = 0; term < words.size(); ++term) {
        auto &posting = spans[t * words.size() + term];
        posting.offset = total;
        if (!bitmaps[term].empty() || !block)
          continue;
        size_t estimate = sample_count
                              ? (uint64_t(sample_frequency[term] + 1) * block +
                                 sample_count - 1) /
                                    sample_count
                              : 0;
        posting.capacity = std::min(block, estimate + 4);
        total += posting.capacity;
      }
      buffer.values.resize(total);
      ShortIDs last(words.size(), UINT16_MAX);
      struct Pending {
        Key token;
        uint64_t hash;
        size_t byte_offset;
        uint32_t term;
      };
      std::array<Pending, 8> pending;
      // Retire in token order so positional postings stay sorted. Only this
      // document's tokens are queued; a document boundary drains every token.
      for (uint32_t id = starts[t]; id < starts[t + 1]; ++id) {
        uint16_t relative = id - starts[t];
        size_t seen = 0;
        auto retire = [&](const Pending &item) {
          const auto &token = item.token;
          size_t byte_offset = item.byte_offset;
          uint32_t term =
              item.hash ? lookup_prepared(token, item.hash) : item.term;
          if (term != UINT32_MAX && position_ids[term] != UINT32_MAX &&
              texts[id].size() <= UINT16_MAX)
            positions[t][position_ids[term]].push_back(
                (uint32_t(relative) << 16) | uint32_t(byte_offset));
          if (term == UINT32_MAX || last[term] == relative)
            return;
          last[term] = relative;
          if (!bitmaps[term].empty())
            bitmaps[term][id / 64] |= uint64_t{1} << (id % 64);
          else {
            auto &posting = spans[t * words.size() + term];
            if (posting.capacity && posting.count < posting.capacity)
              buffer.values[posting.offset + posting.count] = relative;
            else {
              if (posting.capacity) {
                auto begin = buffer.values.begin() + posting.offset;
                buffer.overflow.emplace_back(begin, begin + posting.count);
                posting.offset = buffer.overflow.size() - 1;
                posting.capacity = 0;
              }
              buffer.overflow[posting.offset].push_back(relative);
            }
            ++posting.count;
          }
        };
        tokenize(texts[id], [&](const Key &token, size_t byte_offset) {
          const auto &cached = hot[hot_slot(token.a)];
          uint64_t hash = 0;
          uint32_t term = cached.id;
          if (cached.key != token) {
            hash = key_hash(token);
            __builtin_prefetch(table.data() + (hash & (capacity - 1)), 0, 1);
            __builtin_prefetch(
                bloom.data() + ((hash >> 12) & (bloom.size() - 1)), 0, 1);
          }
          size_t slot = seen % pending.size();
          if (seen >= pending.size())
            retire(pending[slot]);
          pending[slot] = {token, hash, byte_offset, term};
          ++seen;
          return true;
        });
        for (size_t k = seen - std::min(seen, pending.size()); k < seen; ++k)
          retire(pending[k % pending.size()]);
      }
    });
    // PROFILE_INDEX_DONE
    PostingStore store{&buffers, &spans, &starts, words.size(), workers};
    std::vector<PostingView> postings(words.size());
    std::atomic<size_t> next{0};
    parallel(workers, [&](size_t) {
      for (;;) {
        size_t term = next.fetch_add(1);
        if (term >= words.size())
          break;
        size_t count = 0;
        for (size_t t = 0; t < workers; ++t) {
          size_t key = t * words.size() + term;
          count += spans[key].count;
        }
        postings[term] = {&store, term, count};
      }
    });
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
    // PROFILE_READY_TO_QUERY
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
              q.terms.begin(), q.terms.end(), [&](auto x, auto y) {
                if ((position_ids[x] != UINT32_MAX) !=
                    (position_ids[y] != UINT32_MAX))
                  return position_ids[x] != UINT32_MAX;
                return frequencies[x] < frequencies[y];
              });
          size_t anchor_index =
              std::find(q.terms.begin(), q.terms.end(), anchor) -
              q.terms.begin();
          auto candidate_doc = [&](uint32_t doc) {
            if (position_ids[anchor] != UINT32_MAX &&
                texts[doc].size() <= UINT16_MAX) {
              size_t worker =
                  std::upper_bound(starts.begin(), starts.end(), doc) -
                  starts.begin() - 1;
              const auto &list = positions[worker][position_ids[anchor]];
              uint32_t base = (doc - starts[worker]) << 16;
              auto occurrence =
                  std::lower_bound(list.begin(), list.end(), base);
              const char *begin = texts[doc].data(),
                         *end = begin + texts[doc].size();
              auto equal_word = [&](const char *first, const char *last,
                                    uint32_t term) {
                auto expected = words[term];
                if (size_t(last - first) != expected.size())
                  return false;
                for (size_t k = 0; k < expected.size(); ++k)
                  if ((static_cast<unsigned char>(first[k]) | 32) !=
                      static_cast<unsigned char>(expected[k]))
                    return false;
                return true;
              };
              for (; occurrence != list.end() &&
                     (*occurrence >> 16) == (base >> 16);
                   ++occurrence) {
                const char *at = begin + (*occurrence & UINT16_MAX);
                bool found = true;
                for (size_t k = anchor_index; k > 0; --k) {
                  while (at > begin && !word_byte(at[-1]))
                    --at;
                  const char *last = at;
                  while (at > begin && word_byte(at[-1]))
                    --at;
                  if (!equal_word(at, last, q.terms[k - 1])) {
                    found = false;
                    break;
                  }
                }
                if (!found)
                  continue;
                at = begin + (*occurrence & UINT16_MAX) + words[anchor].size();
                for (size_t k = anchor_index + 1; k < q.terms.size(); ++k) {
                  while (at < end && !word_byte(*at))
                    ++at;
                  const char *first = at;
                  while (at < end && word_byte(*at))
                    ++at;
                  if (!equal_word(first, at, q.terms[k])) {
                    found = false;
                    break;
                  }
                }
                if (found) {
                  a.push_back(doc);
                  return;
                }
              }
              return;
            }
            bool candidate = true;
            for (auto term : q.terms)
              if (term != anchor) {
                bool contains = bitmaps[term].empty()
                                    ? postings[term].contains(doc)
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
            tokenize(texts[doc], [&](const Key &key) {
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
    // PROFILE_QUERIES_DONE
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
