#ifndef RAPIDBENCH_CPU_APPLICATION_WORKLOAD_H_
#define RAPIDBENCH_CPU_APPLICATION_WORKLOAD_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace benchmark {

enum class CpuApplicationTest : std::uint32_t {
  kSort = 1,
  kJson = 2,
  kSobel = 3,
};
constexpr std::uint32_t kCpuApplicationMethodVersion = 1;
constexpr std::uint32_t kApplicationSortKeys = 65536;
constexpr std::uint32_t kApplicationJsonRows = 8192;
constexpr std::uint32_t kApplicationImageSide = 1024;

struct JsonRecordSummary {
  std::uint64_t rows = 0;
  std::uint64_t id_sum = 0;
  std::uint64_t value_sum = 0;
  std::uint64_t active_count = 0;
  std::uint64_t name_hash = 14695981039346656037ULL;
  bool operator==(const JsonRecordSummary &other) const;
};

// A deliberately bounded ASCII JSON-record schema, not a general JSON library.
// Keys are id/value/active/name in that order. Whitespace is accepted; escapes,
// negative numbers and other JSON types are outside this workload's schema.
bool ParseApplicationJson(std::string_view input, JsonRecordSummary *result);
void ApplicationSobel(const std::vector<std::uint8_t> &input,
                      std::uint32_t width, std::uint32_t height,
                      std::vector<std::uint8_t> *output);

// Preparation and full reference comparison are never part of timed RunBatch.
// Each worker owns an instance; these tests measure independent-job rate.
class CpuApplicationWorkload final {
public:
  explicit CpuApplicationWorkload(CpuApplicationTest test);
  std::uint64_t RunBatch();
  bool Validate() const;
  std::uint64_t Digest() const;
  std::uint64_t UnitsPerBatch() const;
  std::uint64_t InputBytes() const;

private:
  CpuApplicationTest test_;
  std::vector<std::uint32_t> keys_, sorted_, reference_keys_;
  std::string json_;
  JsonRecordSummary parsed_, reference_json_;
  bool parse_valid_ = false;
  std::vector<std::uint8_t> image_, filtered_, reference_image_;
};

} // namespace benchmark
#endif
