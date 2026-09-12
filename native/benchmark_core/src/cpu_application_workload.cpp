#include "benchmark/cpu_application_workload.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace benchmark {
namespace {

std::uint32_t Next(std::uint32_t *state) {
  *state ^= *state << 13U;
  *state ^= *state >> 17U;
  *state ^= *state << 5U;
  return *state;
}
std::uint64_t Hash(std::uint64_t hash, std::string_view text) {
  for (const unsigned char value : text) {
    hash = (hash ^ value) * 1099511628211ULL;
  }
  return hash;
}

class Cursor {
public:
  explicit Cursor(std::string_view input) : input_(input) {}
  void Space() {
    while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\n' ||
                                   input_[at_] == '\r' || input_[at_] == '\t'))
      ++at_;
  }
  bool Token(std::string_view token) {
    Space();
    if (input_.substr(at_, token.size()) != token)
      return false;
    at_ += token.size();
    return true;
  }
  bool Number(std::uint64_t *value) {
    Space();
    const auto start = at_;
    *value = 0;
    while (at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9') {
      const auto digit = static_cast<std::uint64_t>(input_[at_++] - '0');
      if (*value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
        return false;
      *value = *value * 10 + digit;
    }
    return at_ != start && !(at_ - start > 1 && input_[start] == '0');
  }
  bool Name(std::uint64_t *hash) {
    if (!Token("\""))
      return false;
    const auto start = at_;
    while (at_ < input_.size() && input_[at_] != '"') {
      const auto value = static_cast<unsigned char>(input_[at_++]);
      if (value < 32 || value >= 127 || value == '\\')
        return false;
    }
    if (at_ == input_.size())
      return false;
    *hash = Hash(*hash, input_.substr(start, at_ - start));
    ++at_;
    return true;
  }
  bool End() {
    Space();
    return at_ == input_.size();
  }

private:
  std::string_view input_;
  std::size_t at_ = 0;
};

// Independent counting/radix reference; the measured implementation is
// std::sort.
std::vector<std::uint32_t> ReferenceSort(std::vector<std::uint32_t> values) {
  std::vector<std::uint32_t> scratch(values.size());
  for (unsigned shift = 0; shift < 32; shift += 8) {
    std::array<std::size_t, 256> positions{};
    for (auto value : values)
      ++positions[(value >> shift) & 255U];
    std::size_t offset = 0;
    for (auto &position : positions) {
      const auto count = position;
      position = offset;
      offset += count;
    }
    for (auto value : values)
      scratch[positions[(value >> shift) & 255U]++] = value;
    values.swap(scratch);
  }
  return values;
}

std::vector<std::uint8_t> ReferenceSobel(const std::vector<std::uint8_t> &input,
                                         std::uint32_t side) {
  const int horizontal[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
  const int vertical[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
  std::vector<std::uint8_t> output(input.size(), 0);
  for (std::uint32_t y = 1; y + 1 < side; ++y) {
    for (std::uint32_t x = 1; x + 1 < side; ++x) {
      int gx = 0, gy = 0;
      for (unsigned ky = 0; ky < 3; ++ky) {
        for (unsigned kx = 0; kx < 3; ++kx) {
          const int pixel = input[(y + ky - 1) * side + x + kx - 1];
          gx += pixel * horizontal[ky][kx];
          gy += pixel * vertical[ky][kx];
        }
      }
      output[y * side + x] =
          static_cast<std::uint8_t>(std::min(255, std::abs(gx) + std::abs(gy)));
    }
  }
  return output;
}

} // namespace

bool JsonRecordSummary::operator==(const JsonRecordSummary &other) const {
  return rows == other.rows && id_sum == other.id_sum &&
         value_sum == other.value_sum && active_count == other.active_count &&
         name_hash == other.name_hash;
}

bool ParseApplicationJson(std::string_view input, JsonRecordSummary *result) {
  if (result == nullptr)
    return false;
  JsonRecordSummary parsed;
  Cursor cursor(input);
  if (!cursor.Token("["))
    return false;
  if (cursor.Token("]")) {
    *result = parsed;
    return cursor.End();
  }
  do {
    std::uint64_t id = 0, value = 0;
    if (!cursor.Token("{") || !cursor.Token("\"id\"") || !cursor.Token(":") ||
        !cursor.Number(&id) || !cursor.Token(",") ||
        !cursor.Token("\"value\"") || !cursor.Token(":") ||
        !cursor.Number(&value) || !cursor.Token(",") ||
        !cursor.Token("\"active\"") || !cursor.Token(":"))
      return false;
    const bool active = cursor.Token("true");
    if (!active && !cursor.Token("false"))
      return false;
    if (!cursor.Token(",") || !cursor.Token("\"name\"") || !cursor.Token(":") ||
        !cursor.Name(&parsed.name_hash) || !cursor.Token("}"))
      return false;
    ++parsed.rows;
    parsed.id_sum += id;
    parsed.value_sum += value;
    parsed.active_count += active ? 1 : 0;
  } while (cursor.Token(","));
  if (!cursor.Token("]") || !cursor.End())
    return false;
  *result = parsed;
  return true;
}

void ApplicationSobel(const std::vector<std::uint8_t> &input,
                      std::uint32_t width, std::uint32_t height,
                      std::vector<std::uint8_t> *output) {
  if (output == nullptr || output == &input || width < 3 || height < 3 ||
      static_cast<std::uint64_t>(width) * height != input.size() ||
      output->size() != input.size()) {
    throw std::invalid_argument("Invalid Sobel buffers");
  }
  std::fill(output->begin(), output->begin() + width, 0);
  std::fill(output->end() - width, output->end(), 0);
  for (std::uint32_t y = 1; y + 1 < height; ++y) {
    const auto *top = input.data() + static_cast<std::size_t>(y - 1) * width;
    const auto *middle = top + width;
    const auto *bottom = middle + width;
    auto *out = output->data() + static_cast<std::size_t>(y) * width;
    out[0] = out[width - 1] = 0;
    for (std::uint32_t x = 1; x + 1 < width; ++x) {
      const int gx = top[x + 1] - top[x - 1] +
                     2 * (middle[x + 1] - middle[x - 1]) + bottom[x + 1] -
                     bottom[x - 1];
      const int gy = bottom[x - 1] - top[x - 1] + 2 * (bottom[x] - top[x]) +
                     bottom[x + 1] - top[x + 1];
      out[x] =
          static_cast<std::uint8_t>(std::min(255, std::abs(gx) + std::abs(gy)));
    }
  }
}

CpuApplicationWorkload::CpuApplicationWorkload(CpuApplicationTest test)
    : test_(test) {
  std::uint32_t random = 0x72415049U;
  switch (test_) {
  case CpuApplicationTest::kSort:
    keys_.resize(kApplicationSortKeys);
    for (auto &key : keys_)
      key = Next(&random);
    // Include duplicates and full-range endpoints in the deterministic corpus.
    keys_[0] = 0;
    keys_[1] = UINT32_MAX;
    keys_[2] = keys_[3];
    sorted_.resize(keys_.size());
    reference_keys_ = ReferenceSort(keys_);
    break;
  case CpuApplicationTest::kJson:
    json_ = "[";
    for (std::uint32_t row = 0; row < kApplicationJsonRows; ++row) {
      const std::uint64_t value = Next(&random) % 1000000U;
      const bool active = row % 3 != 0;
      const std::string name = "record-" + std::to_string(row) + "-sample";
      if (row != 0)
        json_ += ",\n";
      json_ += "{\"id\":" + std::to_string(row) +
               ",\"value\":" + std::to_string(value) +
               ",\"active\":" + (active ? "true" : "false") + ",\"name\":\"" +
               name + "\"}";
      ++reference_json_.rows;
      reference_json_.id_sum += row;
      reference_json_.value_sum += value;
      reference_json_.active_count += active ? 1 : 0;
      reference_json_.name_hash = Hash(reference_json_.name_hash, name);
    }
    json_ += "]";
    break;
  case CpuApplicationTest::kSobel:
    image_.resize(kApplicationImageSide * kApplicationImageSide);
    for (std::uint32_t y = 0; y < kApplicationImageSide; ++y) {
      for (std::uint32_t x = 0; x < kApplicationImageSide; ++x) {
        image_[y * kApplicationImageSide + x] =
            static_cast<std::uint8_t>(3 * x + 5 * y + (Next(&random) & 7U));
      }
    }
    filtered_.resize(image_.size());
    reference_image_ = ReferenceSobel(image_, kApplicationImageSide);
    break;
  default:
    throw std::invalid_argument("Unknown CPU application workload");
  }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
std::uint64_t
CpuApplicationWorkload::RunBatch() {
  switch (test_) {
  case CpuApplicationTest::kSort:
    // Reset is intentionally timed: every pass sorts the same unsorted keys.
    std::copy(keys_.begin(), keys_.end(), sorted_.begin());
    std::sort(sorted_.begin(), sorted_.end());
    break;
  case CpuApplicationTest::kJson:
    parse_valid_ = ParseApplicationJson(json_, &parsed_);
    break;
  case CpuApplicationTest::kSobel:
    ApplicationSobel(image_, kApplicationImageSide, kApplicationImageSide,
                     &filtered_);
    break;
  }
  // The caller observes the final output; this compiler barrier also prevents
  // elimination/hoisting of repeated identical-input batches under LTO.
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "r"(this) : "memory");
#endif
  return UnitsPerBatch();
}

bool CpuApplicationWorkload::Validate() const {
  switch (test_) {
  case CpuApplicationTest::kSort:
    return sorted_ == reference_keys_;
  case CpuApplicationTest::kJson:
    return parse_valid_ && parsed_ == reference_json_;
  case CpuApplicationTest::kSobel:
    return filtered_ == reference_image_;
  }
  return false;
}
std::uint64_t CpuApplicationWorkload::Digest() const {
  std::uint64_t hash = 14695981039346656037ULL;
  if (test_ == CpuApplicationTest::kJson)
    return parsed_.name_hash ^ parsed_.id_sum ^ parsed_.value_sum;
  if (test_ == CpuApplicationTest::kSort) {
    for (auto value : sorted_)
      hash = (hash ^ value) * 1099511628211ULL;
  } else {
    for (auto value : filtered_)
      hash = (hash ^ value) * 1099511628211ULL;
  }
  return hash;
}
std::uint64_t CpuApplicationWorkload::UnitsPerBatch() const {
  switch (test_) {
  case CpuApplicationTest::kSort:
    return keys_.size();
  case CpuApplicationTest::kJson:
    return json_.size();
  case CpuApplicationTest::kSobel:
    return image_.size();
  }
  return 0;
}
std::uint64_t CpuApplicationWorkload::InputBytes() const {
  return test_ == CpuApplicationTest::kSort
             ? keys_.size() * sizeof(std::uint32_t)
             : UnitsPerBatch();
}

} // namespace benchmark
