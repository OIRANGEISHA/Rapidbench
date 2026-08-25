#include "benchmark/memory_frequency.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace benchmark {

namespace {

constexpr std::uint32_t kFrequencyCurrentAvailable = 1U << 0U;
constexpr std::uint32_t kFrequencyMaximumAvailable = 1U << 1U;
constexpr std::uint64_t kMinimumPlausibleHz = 1000000ULL;
constexpr std::uint64_t kMaximumPlausibleHz = 20000000000ULL;

std::string ReadText(const std::string &path) {
  std::ifstream stream(path);
  std::ostringstream value;
  value << stream.rdbuf();
  return value.str();
}

std::uint64_t ReadUnsigned(const std::string &path) {
  std::ifstream stream(path);
  std::uint64_t value = 0U;
  stream >> value;
  return stream.fail() ? 0U : value;
}

std::uint64_t ReadMaximumListValue(const std::string &path) {
  std::ifstream stream(path);
  std::uint64_t value = 0U;
  std::uint64_t maximum = 0U;
  while (stream >> value) {
    maximum = std::max(maximum, value);
  }
  return maximum;
}

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

int DeviceScore(const std::string &value) {
  const std::string lower = Lowercase(value);
  if (lower.find("gpu") != std::string::npos ||
      lower.find("memlat") != std::string::npos ||
      lower.find("bwmon") != std::string::npos ||
      lower.find("bw_hwmon") != std::string::npos ||
      lower.find("latfloor") != std::string::npos ||
      lower.find("cpu") != std::string::npos) {
    return -1;
  }
  int score = 0;
  if (lower.find("ddr") != std::string::npos) {
    score += 8;
  }
  if (lower.find("dram") != std::string::npos) {
    score += 8;
  }
  if (lower.find("dmc") != std::string::npos) {
    score += 6;
  }
  if (lower.find("memory-controller") != std::string::npos) {
    score += 5;
  }
  if (lower.find("mif") != std::string::npos) {
    score += 4;
  }
  return score;
}

bool PlausibleHz(std::uint64_t value) {
  return value >= kMinimumPlausibleHz && value <= kMaximumPlausibleHz;
}

} // namespace

MemoryFrequency ReadMemoryFrequency() {
  MemoryFrequency result{};
  DIR *directory = opendir("/sys/class/devfreq");
  if (directory == nullptr) {
    return result;
  }

  int best_score = 0;
  std::string best_path;
  while (const dirent *entry = readdir(directory)) {
    const std::string entry_name = entry->d_name;
    if (entry_name == "." || entry_name == "..") {
      continue;
    }
    const std::string path = "/sys/class/devfreq/" + entry_name;
    const std::string name = ReadText(path + "/name");
    const int score = std::max(DeviceScore(entry_name), DeviceScore(name));
    if (score > best_score) {
      best_score = score;
      best_path = path;
    }
  }
  closedir(directory);
  if (best_path.empty()) {
    return result;
  }

  const std::uint64_t current = ReadUnsigned(best_path + "/cur_freq");
  std::uint64_t maximum = ReadUnsigned(best_path + "/max_freq");
  if (maximum == 0U) {
    maximum = ReadMaximumListValue(best_path + "/available_frequencies");
  }
  if (PlausibleHz(current)) {
    result.current_hz = current;
    result.flags |= kFrequencyCurrentAvailable;
  }
  if (PlausibleHz(maximum)) {
    result.maximum_hz = maximum;
    result.flags |= kFrequencyMaximumAvailable;
  }
  if (result.flags != 0U) {
    result.status = 0;
  }
  return result;
}

} // namespace benchmark
