#include "benchmark/device_vulkan.h"

namespace benchmark {

std::string QueryVulkanInfoJson() {
  return "{\"status\":\"unavailable\",\"reason\":\"Vulkan query is "
         "Android-only\"}";
}

} // namespace benchmark
