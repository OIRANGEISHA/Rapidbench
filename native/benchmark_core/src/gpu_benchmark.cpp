#include "benchmark/gpu_benchmark.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "fp16_emulated.spv.h"
#include "fp16_emulated_12.spv.h"
#include "fp16_emulated_16.spv.h"
#include "fp16_native.spv.h"
#include "fp16_native_12.spv.h"
#include "fp16_native_16.spv.h"
#include "fp32_compute.spv.h"
#include "fp32_compute_12.spv.h"
#include "fp32_compute_16.spv.h"
#include "int32_compute.spv.h"
#include "memory_bandwidth.spv.h"
#include "mixed_compute.spv.h"

namespace benchmark {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::int32_t kStatusOk = 0;
constexpr std::int32_t kStatusInvalidArgument = -1;
constexpr std::int32_t kStatusBusy = -2;
constexpr std::int32_t kStatusInternalError = -3;
constexpr std::int32_t kErrorVulkanUnavailable = -30;
constexpr std::int32_t kErrorNoComputeQueue = -31;
constexpr std::int32_t kErrorDeviceCreation = -32;
constexpr std::int32_t kErrorPipelineCreation = -33;
constexpr std::int32_t kErrorBufferAllocation = -34;
constexpr std::int32_t kErrorSubmission = -35;
constexpr std::int32_t kErrorInvalidResult = -36;

constexpr std::uint32_t kWorkgroupSize = 64U;
constexpr std::uint32_t kComputeWorkgroups = 1024U;
constexpr std::uint32_t kComputeIterations = 64U;
constexpr std::uint32_t kMemoryLoadsPerInvocation = 16U;
constexpr std::uint64_t kIntOperationsPerInvocation = 5632ULL;
constexpr std::uint64_t kMixedWorkPerInvocation = 256ULL;
constexpr std::uint64_t kComputeRegionBytes = 1024ULL * 1024ULL;
constexpr std::uint32_t kComputeOutputRegions = 16U;
constexpr std::uint64_t kComputeOutputBytes =
    kComputeRegionBytes * kComputeOutputRegions;
constexpr std::uint32_t kFpAutotuneRepetitions = 2U;
constexpr double kTargetBatchSeconds = 0.008;
constexpr std::uint32_t kMaximumDispatchesPerBatch = 256U;
constexpr std::uint64_t kFenceTimeoutNs = 10ULL * 1000ULL * 1000ULL * 1000ULL;

enum class PipelineSlot : std::size_t {
  kFp32_8 = 0,
  kFp32_12 = 1,
  kFp32_16 = 2,
  kFp16Native_8 = 3,
  kFp16Native_12 = 4,
  kFp16Native_16 = 5,
  kFp16Emulated_8 = 6,
  kFp16Emulated_12 = 7,
  kFp16Emulated_16 = 8,
  kInt32 = 9,
  kMixed = 10,
  kMemoryBandwidth = 11,
  kCount = 12,
};

constexpr std::array<std::uint32_t, 3> kFpAccumulatorCounts = {8U, 12U,
                                                               16U};
constexpr std::uint32_t kFpVariantCount =
    static_cast<std::uint32_t>(kFpAccumulatorCounts.size());

std::string EscapeJson(const std::string &value) {
  std::ostringstream output;
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character >= 0x20U) {
        output << character;
      }
      break;
    }
  }
  return output.str();
}

std::string VersionString(std::uint32_t version) {
  return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
         std::to_string(VK_API_VERSION_MINOR(version)) + "." +
         std::to_string(VK_API_VERSION_PATCH(version));
}

bool IsRunningState(GpuState state) {
  return state == GpuState::kWarmingUp || state == GpuState::kRunning ||
         state == GpuState::kStopping;
}

struct VulkanFunctions {
  void *loader = nullptr;
  PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
  PFN_vkCreateInstance CreateInstance = nullptr;
  PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties
      EnumerateInstanceExtensionProperties = nullptr;

  PFN_vkDestroyInstance DestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties
      GetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties =
      nullptr;
  PFN_vkEnumerateDeviceExtensionProperties
      EnumerateDeviceExtensionProperties = nullptr;
  PFN_vkCreateDevice CreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;

  PFN_vkDestroyDevice DestroyDevice = nullptr;
  PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
  PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
  PFN_vkCreateBuffer CreateBuffer = nullptr;
  PFN_vkDestroyBuffer DestroyBuffer = nullptr;
  PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
  PFN_vkAllocateMemory AllocateMemory = nullptr;
  PFN_vkFreeMemory FreeMemory = nullptr;
  PFN_vkBindBufferMemory BindBufferMemory = nullptr;
  PFN_vkMapMemory MapMemory = nullptr;
  PFN_vkUnmapMemory UnmapMemory = nullptr;
  PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges = nullptr;
  PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges = nullptr;
  PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
  PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
  PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
  PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
  PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
  PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
  PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
  PFN_vkCreateShaderModule CreateShaderModule = nullptr;
  PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
  PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
  PFN_vkDestroyPipeline DestroyPipeline = nullptr;
  PFN_vkCreateCommandPool CreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
  PFN_vkResetCommandPool ResetCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
  PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
  PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
  PFN_vkCmdPushConstants CmdPushConstants = nullptr;
  PFN_vkCmdDispatch CmdDispatch = nullptr;
  PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
  PFN_vkCreateQueryPool CreateQueryPool = nullptr;
  PFN_vkDestroyQueryPool DestroyQueryPool = nullptr;
  PFN_vkCmdResetQueryPool CmdResetQueryPool = nullptr;
  PFN_vkCmdWriteTimestamp CmdWriteTimestamp = nullptr;
  PFN_vkGetQueryPoolResults GetQueryPoolResults = nullptr;
  PFN_vkCreateFence CreateFence = nullptr;
  PFN_vkDestroyFence DestroyFence = nullptr;
  PFN_vkResetFences ResetFences = nullptr;
  PFN_vkWaitForFences WaitForFences = nullptr;
  PFN_vkQueueSubmit QueueSubmit = nullptr;

  ~VulkanFunctions() {
    if (loader != nullptr) {
      dlclose(loader);
    }
  }

  bool LoadGlobal() {
    loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (loader == nullptr) {
      return false;
    }
    GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(loader, "vkGetInstanceProcAddr"));
    if (GetInstanceProcAddr == nullptr) {
      return false;
    }
#define RAPIDBENCH_LOAD_GLOBAL(name)                                         \
  name = reinterpret_cast<PFN_vk##name>(                                    \
      GetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name))
    RAPIDBENCH_LOAD_GLOBAL(CreateInstance);
    RAPIDBENCH_LOAD_GLOBAL(EnumerateInstanceVersion);
    RAPIDBENCH_LOAD_GLOBAL(EnumerateInstanceExtensionProperties);
#undef RAPIDBENCH_LOAD_GLOBAL
    return CreateInstance != nullptr;
  }

  bool LoadInstance(VkInstance instance) {
#define RAPIDBENCH_LOAD_INSTANCE(name)                                       \
  name = reinterpret_cast<PFN_vk##name>(GetInstanceProcAddr(instance,        \
                                                              "vk" #name))
    RAPIDBENCH_LOAD_INSTANCE(DestroyInstance);
    RAPIDBENCH_LOAD_INSTANCE(EnumeratePhysicalDevices);
    RAPIDBENCH_LOAD_INSTANCE(GetPhysicalDeviceProperties);
    RAPIDBENCH_LOAD_INSTANCE(GetPhysicalDeviceFeatures);
    RAPIDBENCH_LOAD_INSTANCE(GetPhysicalDeviceFeatures2);
    if (GetPhysicalDeviceFeatures2 == nullptr) {
      GetPhysicalDeviceFeatures2 =
          reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
              GetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR"));
    }
    RAPIDBENCH_LOAD_INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
    RAPIDBENCH_LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties);
    RAPIDBENCH_LOAD_INSTANCE(EnumerateDeviceExtensionProperties);
    RAPIDBENCH_LOAD_INSTANCE(CreateDevice);
    RAPIDBENCH_LOAD_INSTANCE(GetDeviceProcAddr);
#undef RAPIDBENCH_LOAD_INSTANCE
    return DestroyInstance != nullptr && EnumeratePhysicalDevices != nullptr &&
           GetPhysicalDeviceProperties != nullptr &&
           GetPhysicalDeviceFeatures != nullptr &&
           GetPhysicalDeviceQueueFamilyProperties != nullptr &&
           GetPhysicalDeviceMemoryProperties != nullptr &&
           EnumerateDeviceExtensionProperties != nullptr &&
           CreateDevice != nullptr && GetDeviceProcAddr != nullptr;
  }

  bool LoadDevice(VkDevice device) {
#define RAPIDBENCH_LOAD_DEVICE(name)                                         \
  name = reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device,            \
                                                            "vk" #name))
    RAPIDBENCH_LOAD_DEVICE(DestroyDevice);
    RAPIDBENCH_LOAD_DEVICE(GetDeviceQueue);
    RAPIDBENCH_LOAD_DEVICE(DeviceWaitIdle);
    RAPIDBENCH_LOAD_DEVICE(CreateBuffer);
    RAPIDBENCH_LOAD_DEVICE(DestroyBuffer);
    RAPIDBENCH_LOAD_DEVICE(GetBufferMemoryRequirements);
    RAPIDBENCH_LOAD_DEVICE(AllocateMemory);
    RAPIDBENCH_LOAD_DEVICE(FreeMemory);
    RAPIDBENCH_LOAD_DEVICE(BindBufferMemory);
    RAPIDBENCH_LOAD_DEVICE(MapMemory);
    RAPIDBENCH_LOAD_DEVICE(UnmapMemory);
    RAPIDBENCH_LOAD_DEVICE(FlushMappedMemoryRanges);
    RAPIDBENCH_LOAD_DEVICE(InvalidateMappedMemoryRanges);
    RAPIDBENCH_LOAD_DEVICE(CreateDescriptorSetLayout);
    RAPIDBENCH_LOAD_DEVICE(DestroyDescriptorSetLayout);
    RAPIDBENCH_LOAD_DEVICE(CreateDescriptorPool);
    RAPIDBENCH_LOAD_DEVICE(DestroyDescriptorPool);
    RAPIDBENCH_LOAD_DEVICE(AllocateDescriptorSets);
    RAPIDBENCH_LOAD_DEVICE(UpdateDescriptorSets);
    RAPIDBENCH_LOAD_DEVICE(CreatePipelineLayout);
    RAPIDBENCH_LOAD_DEVICE(DestroyPipelineLayout);
    RAPIDBENCH_LOAD_DEVICE(CreateShaderModule);
    RAPIDBENCH_LOAD_DEVICE(DestroyShaderModule);
    RAPIDBENCH_LOAD_DEVICE(CreateComputePipelines);
    RAPIDBENCH_LOAD_DEVICE(DestroyPipeline);
    RAPIDBENCH_LOAD_DEVICE(CreateCommandPool);
    RAPIDBENCH_LOAD_DEVICE(DestroyCommandPool);
    RAPIDBENCH_LOAD_DEVICE(ResetCommandPool);
    RAPIDBENCH_LOAD_DEVICE(AllocateCommandBuffers);
    RAPIDBENCH_LOAD_DEVICE(BeginCommandBuffer);
    RAPIDBENCH_LOAD_DEVICE(EndCommandBuffer);
    RAPIDBENCH_LOAD_DEVICE(CmdBindPipeline);
    RAPIDBENCH_LOAD_DEVICE(CmdBindDescriptorSets);
    RAPIDBENCH_LOAD_DEVICE(CmdPushConstants);
    RAPIDBENCH_LOAD_DEVICE(CmdDispatch);
    RAPIDBENCH_LOAD_DEVICE(CmdPipelineBarrier);
    RAPIDBENCH_LOAD_DEVICE(CreateQueryPool);
    RAPIDBENCH_LOAD_DEVICE(DestroyQueryPool);
    RAPIDBENCH_LOAD_DEVICE(CmdResetQueryPool);
    RAPIDBENCH_LOAD_DEVICE(CmdWriteTimestamp);
    RAPIDBENCH_LOAD_DEVICE(GetQueryPoolResults);
    RAPIDBENCH_LOAD_DEVICE(CreateFence);
    RAPIDBENCH_LOAD_DEVICE(DestroyFence);
    RAPIDBENCH_LOAD_DEVICE(ResetFences);
    RAPIDBENCH_LOAD_DEVICE(WaitForFences);
    RAPIDBENCH_LOAD_DEVICE(QueueSubmit);
#undef RAPIDBENCH_LOAD_DEVICE
    return DestroyDevice != nullptr && GetDeviceQueue != nullptr &&
           DeviceWaitIdle != nullptr && CreateBuffer != nullptr &&
           DestroyBuffer != nullptr && GetBufferMemoryRequirements != nullptr &&
           AllocateMemory != nullptr && FreeMemory != nullptr &&
           BindBufferMemory != nullptr && MapMemory != nullptr &&
           UnmapMemory != nullptr && FlushMappedMemoryRanges != nullptr &&
           InvalidateMappedMemoryRanges != nullptr &&
           CreateDescriptorSetLayout != nullptr &&
           DestroyDescriptorSetLayout != nullptr &&
           CreateDescriptorPool != nullptr && DestroyDescriptorPool != nullptr &&
           AllocateDescriptorSets != nullptr && UpdateDescriptorSets != nullptr &&
           CreatePipelineLayout != nullptr && DestroyPipelineLayout != nullptr &&
           CreateShaderModule != nullptr && DestroyShaderModule != nullptr &&
           CreateComputePipelines != nullptr && DestroyPipeline != nullptr &&
           CreateCommandPool != nullptr && DestroyCommandPool != nullptr &&
           ResetCommandPool != nullptr && AllocateCommandBuffers != nullptr &&
           BeginCommandBuffer != nullptr && EndCommandBuffer != nullptr &&
           CmdBindPipeline != nullptr && CmdBindDescriptorSets != nullptr &&
           CmdPushConstants != nullptr && CmdDispatch != nullptr &&
           CmdPipelineBarrier != nullptr && CreateFence != nullptr &&
           DestroyFence != nullptr && ResetFences != nullptr &&
           WaitForFences != nullptr && QueueSubmit != nullptr;
  }
};

struct BufferResource {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0U;
  VkMemoryPropertyFlags properties = 0U;
};

struct BatchResult {
  bool ok = false;
  double seconds = 0.0;
  GpuTimingMode timing_mode = GpuTimingMode::kHostFallback;
  std::string error;
};

class VulkanComputeContext {
public:
  VulkanComputeContext() = default;
  ~VulkanComputeContext() { Destroy(); }

  bool Initialize(std::string *error, std::int32_t *error_code) {
    if (!functions_.LoadGlobal()) {
      return Fail("Vulkan loader is unavailable", kErrorVulkanUnavailable,
                  error, error_code);
    }

    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (functions_.EnumerateInstanceVersion != nullptr) {
      functions_.EnumerateInstanceVersion(&loader_version);
    }
    bool properties2_extension = false;
    if (functions_.EnumerateInstanceExtensionProperties != nullptr) {
      std::uint32_t count = 0U;
      if (functions_.EnumerateInstanceExtensionProperties(nullptr, &count,
                                                           nullptr) ==
              VK_SUCCESS &&
          count > 0U) {
        std::vector<VkExtensionProperties> extensions(count);
        if (functions_.EnumerateInstanceExtensionProperties(
                nullptr, &count, extensions.data()) == VK_SUCCESS) {
          properties2_extension = std::any_of(
              extensions.begin(), extensions.end(), [](const auto &extension) {
                return std::strcmp(
                           extension.extensionName,
                           VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) ==
                       0;
              });
        }
      }
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "RapidBench GPU Compute";
    application.applicationVersion = 1U;
    application.pEngineName = "RapidBench";
    application.engineVersion = 1U;
    application.apiVersion =
        std::min(loader_version, static_cast<std::uint32_t>(VK_API_VERSION_1_2));
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    const char *instance_extension =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    if (loader_version < VK_API_VERSION_1_1 && properties2_extension) {
      instance_info.enabledExtensionCount = 1U;
      instance_info.ppEnabledExtensionNames = &instance_extension;
    }
    if (functions_.CreateInstance(&instance_info, nullptr, &instance_) !=
            VK_SUCCESS ||
        instance_ == VK_NULL_HANDLE) {
      return Fail("Vulkan instance creation failed", kErrorVulkanUnavailable,
                  error, error_code);
    }
    if (!functions_.LoadInstance(instance_)) {
      return Fail("Required Vulkan instance functions are unavailable",
                  kErrorVulkanUnavailable, error, error_code);
    }

    std::uint32_t device_count = 0U;
    if (functions_.EnumeratePhysicalDevices(instance_, &device_count, nullptr) !=
            VK_SUCCESS ||
        device_count == 0U) {
      return Fail("No Vulkan physical device was reported",
                  kErrorVulkanUnavailable, error, error_code);
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    if (functions_.EnumeratePhysicalDevices(instance_, &device_count,
                                             devices.data()) != VK_SUCCESS) {
      return Fail("Vulkan physical-device enumeration failed",
                  kErrorVulkanUnavailable, error, error_code);
    }

    int best_queue_score = -1;
    for (VkPhysicalDevice candidate : devices) {
      std::uint32_t queue_count = 0U;
      functions_.GetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count,
                                                         nullptr);
      if (queue_count == 0U) {
        continue;
      }
      std::vector<VkQueueFamilyProperties> queues(queue_count);
      functions_.GetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count,
                                                         queues.data());
      for (std::uint32_t index = 0; index < queue_count; ++index) {
        if (queues[index].queueCount == 0U ||
            (queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0U) {
          continue;
        }
        const int score = (queues[index].timestampValidBits > 0U ? 2 : 0) +
                          ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) ==
                                   0U
                               ? 1
                               : 0);
        if (score > best_queue_score) {
          best_queue_score = score;
          physical_device_ = candidate;
          queue_family_index_ = index;
          queue_properties_ = queues[index];
        }
      }
    }
    if (physical_device_ == VK_NULL_HANDLE) {
      return Fail("No compute-capable Vulkan queue family was found",
                  kErrorNoComputeQueue, error, error_code);
    }

    functions_.GetPhysicalDeviceProperties(physical_device_, &properties_);
    functions_.GetPhysicalDeviceMemoryProperties(physical_device_,
                                                  &memory_properties_);
    std::uint32_t extension_count = 0U;
    functions_.EnumerateDeviceExtensionProperties(
        physical_device_, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extension_properties(extension_count);
    if (extension_count > 0U) {
      functions_.EnumerateDeviceExtensionProperties(
          physical_device_, nullptr, &extension_count,
          extension_properties.data());
    }
    std::set<std::string> extensions;
    for (const auto &extension : extension_properties) {
      extensions.insert(extension.extensionName);
    }

    const bool has_fp16_extension =
        extensions.find(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) !=
        extensions.end();
    VkPhysicalDeviceShaderFloat16Int8Features fp16_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    if (functions_.GetPhysicalDeviceFeatures2 != nullptr &&
        (properties_.apiVersion >= VK_API_VERSION_1_1 ||
         properties2_extension) &&
        (properties_.apiVersion >= VK_API_VERSION_1_2 ||
         has_fp16_extension)) {
      VkPhysicalDeviceFeatures2 features2{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      features2.pNext = &fp16_features;
      functions_.GetPhysicalDeviceFeatures2(physical_device_, &features2);
      shader_float16_ = fp16_features.shaderFloat16 == VK_TRUE;
    }
    if (properties_.apiVersion < VK_API_VERSION_1_2 && !has_fp16_extension) {
      shader_float16_ = false;
    }

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_index_;
    queue_info.queueCount = 1U;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceShaderFloat16Int8Features enabled_fp16{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    enabled_fp16.shaderFloat16 = shader_float16_ ? VK_TRUE : VK_FALSE;
    std::vector<const char *> enabled_extensions;
    if (shader_float16_ && properties_.apiVersion < VK_API_VERSION_1_2) {
      enabled_extensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
    }
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = shader_float16_ ? &enabled_fp16 : nullptr;
    device_info.queueCreateInfoCount = 1U;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount =
        static_cast<std::uint32_t>(enabled_extensions.size());
    device_info.ppEnabledExtensionNames = enabled_extensions.data();
    if (functions_.CreateDevice(physical_device_, &device_info, nullptr,
                                &device_) != VK_SUCCESS ||
        device_ == VK_NULL_HANDLE) {
      return Fail("Vulkan logical-device creation failed",
                  kErrorDeviceCreation, error, error_code);
    }
    if (!functions_.LoadDevice(device_)) {
      return Fail("Required Vulkan device functions are unavailable",
                  kErrorDeviceCreation, error, error_code);
    }
    functions_.GetDeviceQueue(device_, queue_family_index_, 0U, &queue_);

    if (!CreateCoreResources(error)) {
      *error_code = kErrorPipelineCreation;
      return false;
    }
    available_ = true;
    return true;
  }

  bool available() const { return available_; }
  bool shader_float16() const { return shader_float16_; }
  bool timestamp_available() const {
    return timestamp_usable_ && query_pool_ != VK_NULL_HANDLE;
  }
  std::uint64_t memory_buffer_bytes() const { return memory_input_.size; }
  bool reduced_working_set() const {
    return memory_input_.size > 0U &&
           memory_input_.size < 128ULL * 1024ULL * 1024ULL;
  }
  bool IsFpVariantAvailable(GpuTest test, std::uint32_t fp_variant) const {
    if (fp_variant >= kFpVariantCount) {
      return false;
    }
    return pipelines_[static_cast<std::size_t>(SlotForTest(test, fp_variant))] !=
           VK_NULL_HANDLE;
  }

  std::string CapabilitiesJson(const std::string &initialization_error) const {
    if (!available_) {
      return "{\"status\":\"unavailable\",\"reason\":\"" +
             EscapeJson(initialization_error) + "\"}";
    }
    std::ostringstream output;
    output << "{\"status\":\"available\",\"deviceName\":\""
           << EscapeJson(properties_.deviceName) << "\",\"apiVersion\":\""
           << VersionString(properties_.apiVersion)
           << "\",\"driverVersion\":" << properties_.driverVersion
           << ",\"computeQueueFamily\":" << queue_family_index_
           << ",\"timestampValidBits\":"
           << queue_properties_.timestampValidBits
           << ",\"timestampPeriod\":" << properties_.limits.timestampPeriod
           << ",\"shaderFloat16\":"
           << (shader_float16_ ? "true" : "false")
           << ",\"fp16Mode\":\""
           << (shader_float16_ ? "NATIVE" : "EMULATED")
           << "\",\"workgroupSize\":" << kWorkgroupSize
           << ",\"fpAutotune\":true"
           << ",\"fpAccumulatorVariants\":[8,12,16]"
           << ",\"computeOutputRegions\":" << kComputeOutputRegions
           << ",\"timingMode\":\""
           << (timestamp_available() ? "GPU_TIMESTAMP" : "HOST_FALLBACK")
           << "\",\"maxStorageBufferRange\":"
           << properties_.limits.maxStorageBufferRange
           << ",\"memoryBufferBytes\":" << memory_input_.size
           << ",\"reducedWorkingSet\":"
           << (reduced_working_set() ? "true" : "false") << "}";
    return output.str();
  }

  bool EnsureMemoryBandwidthResources(std::string *error) {
    if (memory_input_.buffer != VK_NULL_HANDLE) {
      return true;
    }
    std::uint64_t largest_heap = 0U;
    for (std::uint32_t index = 0; index < memory_properties_.memoryHeapCount;
         ++index) {
      largest_heap = std::max<std::uint64_t>(
          largest_heap, memory_properties_.memoryHeaps[index].size);
    }
    constexpr std::array<std::uint32_t, 9> candidates_mib = {
        256U, 192U, 128U, 96U, 64U, 48U, 32U, 24U, 16U};
    for (const std::uint32_t mib : candidates_mib) {
      const std::uint64_t bytes = static_cast<std::uint64_t>(mib) *
                                  1024ULL * 1024ULL;
      if (bytes > properties_.limits.maxStorageBufferRange ||
          (largest_heap > 0U && bytes > largest_heap / 4U)) {
        continue;
      }
      BufferResource input{};
      BufferResource output{};
      const std::uint64_t output_bytes = bytes / 64U;
      if (!CreateHostBuffer(bytes, &input) ||
          !CreateHostBuffer(output_bytes, &output)) {
        DestroyBuffer(&input);
        DestroyBuffer(&output);
        continue;
      }
      if (!InitializeBuffer(input, false) || !InitializeBuffer(output, true)) {
        DestroyBuffer(&input);
        DestroyBuffer(&output);
        continue;
      }
      memory_input_ = input;
      memory_output_ = output;
      UpdateDescriptor(PipelineSlot::kMemoryBandwidth, memory_input_,
                       memory_output_);
      return true;
    }
    *error = "Unable to allocate even the reduced GPU bandwidth working set";
    return false;
  }

  BatchResult ExecuteBatch(GpuTest test, std::uint32_t repetitions,
                           std::uint32_t ring_index,
                           std::uint32_t fp_variant = 0U) {
    BatchResult result{};
    const PipelineSlot slot = SlotForTest(test, fp_variant);
    const VkPipeline pipeline = pipelines_[static_cast<std::size_t>(slot)];
    if (pipeline == VK_NULL_HANDLE) {
      result.error = "The requested Vulkan compute pipeline is unavailable";
      return result;
    }
    std::uint32_t workgroups = kComputeWorkgroups;
    std::uint32_t element_count = 0U;
    if (test == GpuTest::kMemoryBandwidth) {
      if (memory_input_.size == 0U) {
        result.error = "GPU bandwidth buffer was not initialized";
        return result;
      }
      element_count = static_cast<std::uint32_t>(memory_input_.size / 16U);
      workgroups = static_cast<std::uint32_t>(
          memory_input_.size / (256ULL * kWorkgroupSize));
    }

    if (functions_.ResetCommandPool(device_, command_pool_, 0U) != VK_SUCCESS) {
      result.error = "Vulkan command-pool reset failed";
      return result;
    }
    VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (functions_.BeginCommandBuffer(command_buffer_, &begin_info) !=
        VK_SUCCESS) {
      result.error = "Vulkan command-buffer begin failed";
      return result;
    }
    const bool use_timestamp = timestamp_available();
    if (use_timestamp) {
      functions_.CmdResetQueryPool(command_buffer_, query_pool_, 0U, 2U);
      functions_.CmdWriteTimestamp(command_buffer_,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   query_pool_, 0U);
    }
    functions_.CmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                               pipeline);
    const VkDescriptorSet descriptor =
        descriptor_sets_[static_cast<std::size_t>(slot)];
    functions_.CmdBindDescriptorSets(
        command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0U,
        1U, &descriptor, 0U, nullptr);
    struct Parameters {
      std::uint32_t first;
      std::uint32_t second;
    };
    constexpr std::uint32_t kComputeInvocations =
        kComputeWorkgroups * kWorkgroupSize;
    for (std::uint32_t repetition = 0U; repetition < repetitions;
         ++repetition) {
      Parameters parameters{};
      if (test == GpuTest::kMemoryBandwidth) {
        parameters.first = element_count;
        const std::uint32_t quarter = element_count / 4U;
        parameters.second =
            ((ring_index + repetition) % 4U) * quarter;
      } else {
        parameters.first =
            (((ring_index * 7U) + repetition) % kComputeOutputRegions) *
            kComputeInvocations;
      }
      functions_.CmdPushConstants(command_buffer_, pipeline_layout_,
                                  VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                                  sizeof(parameters), &parameters);
      functions_.CmdDispatch(command_buffer_, workgroups, 1U, 1U);
      const bool has_more_dispatches = repetition + 1U < repetitions;
      const bool requires_barrier =
          has_more_dispatches &&
          (test == GpuTest::kMemoryBandwidth ||
           (repetition + 1U) % kComputeOutputRegions == 0U);
      if (requires_barrier) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        functions_.CmdPipelineBarrier(
            command_buffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 1U, &barrier, 0U,
            nullptr, 0U, nullptr);
      }
    }
    if (use_timestamp) {
      functions_.CmdWriteTimestamp(command_buffer_,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                   query_pool_, 1U);
    }
    if (functions_.EndCommandBuffer(command_buffer_) != VK_SUCCESS) {
      result.error = "Vulkan command-buffer end failed";
      return result;
    }
    if (functions_.ResetFences(device_, 1U, &fence_) != VK_SUCCESS) {
      result.error = "Vulkan fence reset failed";
      return result;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1U;
    submit.pCommandBuffers = &command_buffer_;
    const auto host_start = Clock::now();
    if (functions_.QueueSubmit(queue_, 1U, &submit, fence_) != VK_SUCCESS) {
      result.error = "Vulkan queue submission failed";
      return result;
    }
    const VkResult wait_result = functions_.WaitForFences(
        device_, 1U, &fence_, VK_TRUE, kFenceTimeoutNs);
    const auto host_end = Clock::now();
    if (wait_result != VK_SUCCESS) {
      result.error = wait_result == VK_TIMEOUT
                         ? "GPU workload timed out"
                         : "Waiting for the GPU workload failed";
      return result;
    }
    result.seconds =
        std::chrono::duration<double>(host_end - host_start).count();
    result.timing_mode = GpuTimingMode::kHostFallback;
    if (use_timestamp) {
      std::array<std::uint64_t, 2> timestamps{};
      const VkResult query_result = functions_.GetQueryPoolResults(
          device_, query_pool_, 0U, 2U, sizeof(timestamps), timestamps.data(),
          sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
      if (query_result == VK_SUCCESS) {
        std::uint64_t ticks = 0U;
        const std::uint32_t valid_bits = queue_properties_.timestampValidBits;
        if (valid_bits >= 64U) {
          ticks = timestamps[1] - timestamps[0];
        } else if (valid_bits > 0U) {
          const std::uint64_t mask = (1ULL << valid_bits) - 1ULL;
          ticks = (timestamps[1] - timestamps[0]) & mask;
        }
        const double gpu_seconds =
            static_cast<double>(ticks) * properties_.limits.timestampPeriod *
            1.0e-9;
        if (gpu_seconds > 0.0 && std::isfinite(gpu_seconds)) {
          result.seconds = gpu_seconds;
          result.timing_mode = GpuTimingMode::kGpuTimestamp;
        } else {
          timestamp_usable_ = false;
        }
      } else {
        timestamp_usable_ = false;
      }
    }
    result.ok = result.seconds > 0.0 && std::isfinite(result.seconds);
    if (!result.ok) {
      result.error = "GPU timing produced an invalid duration";
    }
    return result;
  }

  bool ValidateSink(GpuTest test) {
    const BufferResource &output =
        test == GpuTest::kMemoryBandwidth ? memory_output_ : compute_output_;
    if (output.memory == VK_NULL_HANDLE) {
      return false;
    }
    void *mapped = nullptr;
    if (functions_.MapMemory(device_, output.memory, 0U, output.size, 0U,
                             &mapped) != VK_SUCCESS ||
        mapped == nullptr) {
      return false;
    }
    if ((output.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0U) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = output.memory;
      range.offset = 0U;
      range.size = VK_WHOLE_SIZE;
      functions_.InvalidateMappedMemoryRanges(device_, 1U, &range);
    }
    bool valid = false;
    if (test == GpuTest::kInt32) {
      const auto *values = static_cast<const std::uint32_t *>(mapped);
      for (std::size_t index = 0; index < 16U; ++index) {
        valid = valid || values[index] != 0U;
      }
    } else {
      const auto *values = static_cast<const float *>(mapped);
      for (std::size_t index = 0; index < 16U; ++index) {
        if (std::isfinite(values[index]) && values[index] != 0.0F) {
          valid = true;
          break;
        }
      }
    }
    functions_.UnmapMemory(device_, output.memory);
    return valid;
  }

private:
  static bool Fail(const std::string &message, std::int32_t code,
                   std::string *error, std::int32_t *error_code) {
    *error = message;
    *error_code = code;
    return false;
  }

  bool CreateCoreResources(std::string *error) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (std::uint32_t index = 0; index < bindings.size(); ++index) {
      bindings[index].binding = index;
      bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[index].descriptorCount = 1U;
      bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    if (functions_.CreateDescriptorSetLayout(device_, &layout_info, nullptr,
                                              &descriptor_set_layout_) !=
        VK_SUCCESS) {
      *error = "Vulkan descriptor-set layout creation failed";
      return false;
    }
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0U;
    push_range.size = 8U;
    VkPipelineLayoutCreateInfo pipeline_layout_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1U;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
    pipeline_layout_info.pushConstantRangeCount = 1U;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    if (functions_.CreatePipelineLayout(device_, &pipeline_layout_info, nullptr,
                                        &pipeline_layout_) != VK_SUCCESS) {
      *error = "Vulkan pipeline-layout creation failed";
      return false;
    }
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount =
        static_cast<std::uint32_t>(PipelineSlot::kCount) * 2U;
    VkDescriptorPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = static_cast<std::uint32_t>(PipelineSlot::kCount);
    pool_info.poolSizeCount = 1U;
    pool_info.pPoolSizes = &pool_size;
    if (functions_.CreateDescriptorPool(device_, &pool_info, nullptr,
                                        &descriptor_pool_) != VK_SUCCESS) {
      *error = "Vulkan descriptor-pool creation failed";
      return false;
    }
    std::array<VkDescriptorSetLayout,
               static_cast<std::size_t>(PipelineSlot::kCount)>
        layouts{};
    layouts.fill(descriptor_set_layout_);
    VkDescriptorSetAllocateInfo allocate_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate_info.descriptorPool = descriptor_pool_;
    allocate_info.descriptorSetCount =
        static_cast<std::uint32_t>(layouts.size());
    allocate_info.pSetLayouts = layouts.data();
    if (functions_.AllocateDescriptorSets(device_, &allocate_info,
                                          descriptor_sets_.data()) !=
        VK_SUCCESS) {
      *error = "Vulkan descriptor-set allocation failed";
      return false;
    }
    if (!CreateHostBuffer(kComputeRegionBytes, &compute_input_) ||
        !CreateHostBuffer(kComputeOutputBytes, &compute_output_) ||
        !InitializeBuffer(compute_input_, false) ||
        !InitializeBuffer(compute_output_, true)) {
      *error = "GPU compute buffer allocation failed";
      return false;
    }
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(PipelineSlot::kMemoryBandwidth);
         ++index) {
      UpdateDescriptor(static_cast<PipelineSlot>(index), compute_input_,
                       compute_output_);
    }

    if (!CreatePipeline(PipelineSlot::kFp32_8, gpu_shaders::kFp32Compute,
                        gpu_shaders::kFp32ComputeSize, error) ||
        !CreatePipeline(PipelineSlot::kFp16Emulated_8,
                        gpu_shaders::kFp16Emulated,
                        gpu_shaders::kFp16EmulatedSize, error) ||
        !CreatePipeline(PipelineSlot::kInt32, gpu_shaders::kInt32Compute,
                        gpu_shaders::kInt32ComputeSize, error) ||
        !CreatePipeline(PipelineSlot::kMixed, gpu_shaders::kMixedCompute,
                        gpu_shaders::kMixedComputeSize, error) ||
        !CreatePipeline(PipelineSlot::kMemoryBandwidth,
                        gpu_shaders::kMemoryBandwidth,
                        gpu_shaders::kMemoryBandwidthSize, error)) {
      return false;
    }
    if (shader_float16_ &&
        !CreatePipeline(PipelineSlot::kFp16Native_8,
                        gpu_shaders::kFp16Native,
                        gpu_shaders::kFp16NativeSize, error)) {
      return false;
    }

    TryCreateOptionalPipeline(PipelineSlot::kFp32_12,
                              gpu_shaders::kFp32Compute12,
                              gpu_shaders::kFp32Compute12Size);
    TryCreateOptionalPipeline(PipelineSlot::kFp32_16,
                              gpu_shaders::kFp32Compute16,
                              gpu_shaders::kFp32Compute16Size);
    TryCreateOptionalPipeline(PipelineSlot::kFp16Emulated_12,
                              gpu_shaders::kFp16Emulated12,
                              gpu_shaders::kFp16Emulated12Size);
    TryCreateOptionalPipeline(PipelineSlot::kFp16Emulated_16,
                              gpu_shaders::kFp16Emulated16,
                              gpu_shaders::kFp16Emulated16Size);
    if (shader_float16_) {
      TryCreateOptionalPipeline(PipelineSlot::kFp16Native_12,
                                gpu_shaders::kFp16Native12,
                                gpu_shaders::kFp16Native12Size);
      TryCreateOptionalPipeline(PipelineSlot::kFp16Native_16,
                                gpu_shaders::kFp16Native16,
                                gpu_shaders::kFp16Native16Size);
    }

    VkCommandPoolCreateInfo command_pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = queue_family_index_;
    if (functions_.CreateCommandPool(device_, &command_pool_info, nullptr,
                                     &command_pool_) != VK_SUCCESS) {
      *error = "Vulkan command-pool creation failed";
      return false;
    }
    VkCommandBufferAllocateInfo command_allocate{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_allocate.commandPool = command_pool_;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1U;
    if (functions_.AllocateCommandBuffers(device_, &command_allocate,
                                          &command_buffer_) != VK_SUCCESS) {
      *error = "Vulkan command-buffer allocation failed";
      return false;
    }
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (functions_.CreateFence(device_, &fence_info, nullptr, &fence_) !=
        VK_SUCCESS) {
      *error = "Vulkan fence creation failed";
      return false;
    }
    if (queue_properties_.timestampValidBits > 0U &&
        properties_.limits.timestampPeriod > 0.0F &&
        functions_.CreateQueryPool != nullptr &&
        functions_.DestroyQueryPool != nullptr &&
        functions_.CmdResetQueryPool != nullptr &&
        functions_.CmdWriteTimestamp != nullptr &&
        functions_.GetQueryPoolResults != nullptr) {
      VkQueryPoolCreateInfo query_info{
          VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
      query_info.queryCount = 2U;
      if (functions_.CreateQueryPool(device_, &query_info, nullptr,
                                     &query_pool_) == VK_SUCCESS) {
        timestamp_usable_ = true;
      }
    }
    return true;
  }

  bool CreatePipeline(PipelineSlot slot, const std::uint8_t *code,
                      std::size_t code_size, std::string *error) {
    if (code_size == 0U || code_size % sizeof(std::uint32_t) != 0U) {
      *error = "Embedded SPIR-V has an invalid size";
      return false;
    }
    VkShaderModuleCreateInfo shader_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = code_size;
    shader_info.pCode = reinterpret_cast<const std::uint32_t *>(code);
    VkShaderModule shader = VK_NULL_HANDLE;
    if (functions_.CreateShaderModule(device_, &shader_info, nullptr, &shader) !=
        VK_SUCCESS) {
      *error = "Vulkan shader-module creation failed";
      return false;
    }
    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";
    VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = stage;
    pipeline_info.layout = pipeline_layout_;
    const VkResult result = functions_.CreateComputePipelines(
        device_, VK_NULL_HANDLE, 1U, &pipeline_info, nullptr,
        &pipelines_[static_cast<std::size_t>(slot)]);
    functions_.DestroyShaderModule(device_, shader, nullptr);
    if (result != VK_SUCCESS) {
      *error = "Vulkan compute-pipeline creation failed";
      return false;
    }
    return true;
  }

  void TryCreateOptionalPipeline(PipelineSlot slot, const std::uint8_t *code,
                                 std::size_t code_size) {
    std::string ignored_error;
    if (CreatePipeline(slot, code, code_size, &ignored_error)) {
      return;
    }
    VkPipeline &pipeline = pipelines_[static_cast<std::size_t>(slot)];
    if (pipeline != VK_NULL_HANDLE) {
      functions_.DestroyPipeline(device_, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
  }

  std::optional<std::pair<std::uint32_t, VkMemoryPropertyFlags>> FindMemoryType(
      std::uint32_t type_bits, VkMemoryPropertyFlags required,
      VkMemoryPropertyFlags preferred) const {
    for (int pass = 0; pass < 2; ++pass) {
      const VkMemoryPropertyFlags wanted = pass == 0 ? required | preferred
                                                     : required;
      for (std::uint32_t index = 0; index < memory_properties_.memoryTypeCount;
           ++index) {
        if ((type_bits & (1U << index)) != 0U &&
            (memory_properties_.memoryTypes[index].propertyFlags & wanted) ==
                wanted) {
          return std::make_pair(
              index, memory_properties_.memoryTypes[index].propertyFlags);
        }
      }
    }
    return std::nullopt;
  }

  bool CreateHostBuffer(VkDeviceSize size, BufferResource *resource) {
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (functions_.CreateBuffer(device_, &buffer_info, nullptr,
                                &resource->buffer) != VK_SUCCESS) {
      return false;
    }
    VkMemoryRequirements requirements{};
    functions_.GetBufferMemoryRequirements(device_, resource->buffer,
                                            &requirements);
    const auto memory_type = FindMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memory_type.has_value()) {
      DestroyBuffer(resource);
      return false;
    }
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type->first;
    if (functions_.AllocateMemory(device_, &allocation, nullptr,
                                  &resource->memory) != VK_SUCCESS) {
      DestroyBuffer(resource);
      return false;
    }
    if (functions_.BindBufferMemory(device_, resource->buffer,
                                    resource->memory, 0U) != VK_SUCCESS) {
      DestroyBuffer(resource);
      return false;
    }
    resource->size = size;
    resource->properties = memory_type->second;
    return true;
  }

  bool InitializeBuffer(const BufferResource &resource, bool clear) {
    void *mapped = nullptr;
    if (functions_.MapMemory(device_, resource.memory, 0U, resource.size, 0U,
                             &mapped) != VK_SUCCESS ||
        mapped == nullptr) {
      return false;
    }
    if (clear) {
      std::memset(mapped, 0, static_cast<std::size_t>(resource.size));
    } else {
      auto *words = static_cast<std::uint32_t *>(mapped);
      const std::size_t word_count =
          static_cast<std::size_t>(resource.size / sizeof(std::uint32_t));
      for (std::size_t index = 0; index < word_count; ++index) {
        words[index] = 0x3E800000U +
                       static_cast<std::uint32_t>((index * 2654435761ULL) &
                                                  0x000FFFFFU);
      }
    }
    if ((resource.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0U) {
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = resource.memory;
      range.offset = 0U;
      range.size = VK_WHOLE_SIZE;
      if (functions_.FlushMappedMemoryRanges(device_, 1U, &range) !=
          VK_SUCCESS) {
        functions_.UnmapMemory(device_, resource.memory);
        return false;
      }
    }
    functions_.UnmapMemory(device_, resource.memory);
    return true;
  }

  void UpdateDescriptor(PipelineSlot slot, const BufferResource &input,
                        const BufferResource &output) {
    std::array<VkDescriptorBufferInfo, 2> buffer_infos{};
    buffer_infos[0].buffer = input.buffer;
    buffer_infos[0].offset = 0U;
    buffer_infos[0].range = input.size;
    buffer_infos[1].buffer = output.buffer;
    buffer_infos[1].offset = 0U;
    buffer_infos[1].range = output.size;
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::uint32_t index = 0; index < writes.size(); ++index) {
      writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[index].dstSet = descriptor_sets_[static_cast<std::size_t>(slot)];
      writes[index].dstBinding = index;
      writes[index].descriptorCount = 1U;
      writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[index].pBufferInfo = &buffer_infos[index];
    }
    functions_.UpdateDescriptorSets(device_,
                                    static_cast<std::uint32_t>(writes.size()),
                                    writes.data(), 0U, nullptr);
  }

  PipelineSlot SlotForTest(GpuTest test, std::uint32_t fp_variant) const {
    const std::size_t variant = std::min<std::size_t>(
        fp_variant, static_cast<std::size_t>(kFpVariantCount - 1U));
    constexpr std::array<PipelineSlot, 3> fp32_slots = {
        PipelineSlot::kFp32_8, PipelineSlot::kFp32_12,
        PipelineSlot::kFp32_16};
    constexpr std::array<PipelineSlot, 3> fp16_native_slots = {
        PipelineSlot::kFp16Native_8, PipelineSlot::kFp16Native_12,
        PipelineSlot::kFp16Native_16};
    constexpr std::array<PipelineSlot, 3> fp16_emulated_slots = {
        PipelineSlot::kFp16Emulated_8, PipelineSlot::kFp16Emulated_12,
        PipelineSlot::kFp16Emulated_16};
    switch (test) {
    case GpuTest::kFp32:
      return fp32_slots[variant];
    case GpuTest::kFp16:
      return shader_float16_ ? fp16_native_slots[variant]
                             : fp16_emulated_slots[variant];
    case GpuTest::kInt32:
      return PipelineSlot::kInt32;
    case GpuTest::kMixed:
      return PipelineSlot::kMixed;
    case GpuTest::kMemoryBandwidth:
      return PipelineSlot::kMemoryBandwidth;
    default:
      return PipelineSlot::kFp32_8;
    }
  }

  void DestroyBuffer(BufferResource *resource) {
    if (device_ != VK_NULL_HANDLE && resource->buffer != VK_NULL_HANDLE &&
        functions_.DestroyBuffer != nullptr) {
      functions_.DestroyBuffer(device_, resource->buffer, nullptr);
    }
    if (device_ != VK_NULL_HANDLE && resource->memory != VK_NULL_HANDLE &&
        functions_.FreeMemory != nullptr) {
      functions_.FreeMemory(device_, resource->memory, nullptr);
    }
    *resource = {};
  }

  void Destroy() {
    if (device_ != VK_NULL_HANDLE && functions_.DeviceWaitIdle != nullptr) {
      functions_.DeviceWaitIdle(device_);
    }
    if (device_ != VK_NULL_HANDLE) {
      if (fence_ != VK_NULL_HANDLE && functions_.DestroyFence != nullptr) {
        functions_.DestroyFence(device_, fence_, nullptr);
      }
      if (query_pool_ != VK_NULL_HANDLE &&
          functions_.DestroyQueryPool != nullptr) {
        functions_.DestroyQueryPool(device_, query_pool_, nullptr);
      }
      if (command_pool_ != VK_NULL_HANDLE &&
          functions_.DestroyCommandPool != nullptr) {
        functions_.DestroyCommandPool(device_, command_pool_, nullptr);
      }
      for (VkPipeline pipeline : pipelines_) {
        if (pipeline != VK_NULL_HANDLE && functions_.DestroyPipeline != nullptr) {
          functions_.DestroyPipeline(device_, pipeline, nullptr);
        }
      }
      if (pipeline_layout_ != VK_NULL_HANDLE &&
          functions_.DestroyPipelineLayout != nullptr) {
        functions_.DestroyPipelineLayout(device_, pipeline_layout_, nullptr);
      }
      if (descriptor_pool_ != VK_NULL_HANDLE &&
          functions_.DestroyDescriptorPool != nullptr) {
        functions_.DestroyDescriptorPool(device_, descriptor_pool_, nullptr);
      }
      if (descriptor_set_layout_ != VK_NULL_HANDLE &&
          functions_.DestroyDescriptorSetLayout != nullptr) {
        functions_.DestroyDescriptorSetLayout(device_, descriptor_set_layout_,
                                              nullptr);
      }
      DestroyBuffer(&memory_output_);
      DestroyBuffer(&memory_input_);
      DestroyBuffer(&compute_output_);
      DestroyBuffer(&compute_input_);
      if (functions_.DestroyDevice != nullptr) {
        functions_.DestroyDevice(device_, nullptr);
      }
    }
    device_ = VK_NULL_HANDLE;
    if (instance_ != VK_NULL_HANDLE && functions_.DestroyInstance != nullptr) {
      functions_.DestroyInstance(instance_, nullptr);
    }
    instance_ = VK_NULL_HANDLE;
    available_ = false;
  }

  VulkanFunctions functions_{};
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queue_family_index_ = 0U;
  VkQueueFamilyProperties queue_properties_{};
  VkPhysicalDeviceProperties properties_{};
  VkPhysicalDeviceMemoryProperties memory_properties_{};
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSet,
             static_cast<std::size_t>(PipelineSlot::kCount)>
      descriptor_sets_{};
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  std::array<VkPipeline, static_cast<std::size_t>(PipelineSlot::kCount)>
      pipelines_{};
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkQueryPool query_pool_ = VK_NULL_HANDLE;
  BufferResource compute_input_{};
  BufferResource compute_output_{};
  BufferResource memory_input_{};
  BufferResource memory_output_{};
  bool shader_float16_ = false;
  bool timestamp_usable_ = false;
  bool available_ = false;
};

double WorkAmount(GpuTest test, std::uint32_t repetitions,
                  std::uint64_t memory_bytes,
                  std::uint32_t fp_accumulators) {
  const std::uint64_t invocations =
      static_cast<std::uint64_t>(kComputeWorkgroups) * kWorkgroupSize;
  switch (test) {
  case GpuTest::kFp32:
  case GpuTest::kFp16: {
    const std::uint64_t fp_operations_per_invocation =
        static_cast<std::uint64_t>(fp_accumulators) * 4ULL * 2ULL *
        kComputeIterations;
    return static_cast<double>(invocations) *
           static_cast<double>(fp_operations_per_invocation) * repetitions;
  }
  case GpuTest::kInt32:
    return static_cast<double>(invocations) *
           static_cast<double>(kIntOperationsPerInvocation) * repetitions;
  case GpuTest::kMixed:
    return static_cast<double>(invocations) *
           static_cast<double>(kMixedWorkPerInvocation) * repetitions;
  case GpuTest::kMemoryBandwidth:
    return static_cast<double>(memory_bytes) * repetitions;
  default:
    return 0.0;
  }
}

} // namespace

class GpuBenchmarkEngine::Impl {
public:
  Impl() {
    std::int32_t error_code = 0;
    context_available_ =
        context_.Initialize(&initialization_error_, &error_code);
    snapshot_.vulkan_available = context_available_;
    snapshot_.fp16_mode = context_.shader_float16()
                                ? GpuFp16Mode::kNative
                                : GpuFp16Mode::kEmulated;
    snapshot_.timing_mode = context_.timestamp_available()
                                ? GpuTimingMode::kGpuTimestamp
                                : GpuTimingMode::kHostFallback;
    if (!context_available_) {
      snapshot_.last_error = initialization_error_;
      initialization_error_code_ = error_code;
    }
  }

  ~Impl() {
    stop_requested_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::int32_t Start(const GpuRequest &request, std::uint64_t *out_run_id) {
    if (out_run_id == nullptr || request.test == GpuTest::kNone ||
        static_cast<std::uint32_t>(request.test) >
            static_cast<std::uint32_t>(GpuTest::kAll)) {
      return kStatusInvalidArgument;
    }
    if (!context_available_) {
      return kStatusInternalError;
    }
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      if (IsRunningState(snapshot_.state)) {
        return kStatusBusy;
      }
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    stop_requested_.store(false, std::memory_order_release);
    GpuRequest normalized = request;
    normalized.duration_ms = std::clamp(normalized.duration_ms, 500U, 15000U);
    normalized.warmup_ms = std::clamp(normalized.warmup_ms, 100U, 1500U);
    const std::uint64_t run_id = next_run_id_++;
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_.run_id = run_id;
      snapshot_.state = GpuState::kWarmingUp;
      snapshot_.active_test = request.test == GpuTest::kAll
                                  ? GpuTest::kFp32
                                  : request.test;
      snapshot_.error_code = 0;
      snapshot_.elapsed_ns = 0U;
      snapshot_.progress = 0.0;
      snapshot_.gpu_batch_ms = 0.0;
      snapshot_.dispatch_count = 0U;
      snapshot_.last_error.clear();
    }
    try {
      worker_ = std::thread(&Impl::WorkerMain, this, normalized, run_id);
    } catch (...) {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_.state = GpuState::kError;
      snapshot_.error_code = kStatusInternalError;
      snapshot_.last_error = "GPU benchmark worker creation failed";
      return kStatusInternalError;
    }
    *out_run_id = run_id;
    return kStatusOk;
  }

  std::int32_t RequestStop(std::uint64_t run_id) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id || !IsRunningState(snapshot_.state)) {
      return kStatusInvalidArgument;
    }
    stop_requested_.store(true, std::memory_order_release);
    snapshot_.state = GpuState::kStopping;
    return kStatusOk;
  }

  GpuSnapshot GetSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
  }

  std::string GetCapabilitiesJson() const {
    return context_.CapabilitiesJson(initialization_error_);
  }

private:
  void WorkerMain(GpuRequest request, std::uint64_t run_id) {
    const auto run_start = Clock::now();
    const std::array<GpuTest, 5> all_tests = {
        GpuTest::kFp32, GpuTest::kFp16, GpuTest::kInt32,
        GpuTest::kMixed, GpuTest::kMemoryBandwidth};
    if (request.test == GpuTest::kAll) {
      for (std::size_t index = 0; index < all_tests.size(); ++index) {
        if (!RunTest(all_tests[index], request, run_id, run_start, index,
                     all_tests.size())) {
          return;
        }
        if (index + 1U < all_tests.size()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
      }
    } else if (!RunTest(request.test, request, run_id, run_start, 0U, 1U)) {
      return;
    }

    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (stop_requested_.load(std::memory_order_acquire)) {
      snapshot_.state = GpuState::kStopped;
    } else {
      snapshot_.state = GpuState::kCompleted;
      snapshot_.active_test = GpuTest::kNone;
      snapshot_.progress = 1.0;
      // Overall score intentionally remains unchanged until calibration exists.
    }
    snapshot_.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             run_start)
            .count());
  }

  bool RunTest(GpuTest test, const GpuRequest &request, std::uint64_t run_id,
               Clock::time_point run_start, std::size_t sequence_index,
               std::size_t sequence_count) {
    if (test == GpuTest::kMemoryBandwidth) {
      std::string allocation_error;
      if (!context_.EnsureMemoryBandwidthResources(&allocation_error)) {
        SetError(run_id, kErrorBufferAllocation, allocation_error, run_start);
        return false;
      }
    }
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot_.state = GpuState::kWarmingUp;
      snapshot_.active_test = test;
      snapshot_.buffer_bytes = context_.memory_buffer_bytes();
      snapshot_.reduced_working_set = context_.reduced_working_set();
      snapshot_.workgroup_size = kWorkgroupSize;
      snapshot_.iteration_count =
          test == GpuTest::kMemoryBandwidth ? kMemoryLoadsPerInvocation
                                            : kComputeIterations;
    }

    std::uint32_t repetitions = 1U;
    std::uint32_t ring_index = 0U;
    std::uint32_t selected_fp_variant = 0U;
    std::uint32_t fp_accumulators = kFpAccumulatorCounts[0];
    const bool is_fp_test = test == GpuTest::kFp32 || test == GpuTest::kFp16;
    if (is_fp_test) {
      std::array<double, kFpVariantCount> accumulated_throughput{};
      std::array<std::uint32_t, kFpVariantCount> sample_counts{};
      for (std::uint32_t pass = 0U; pass < 3U; ++pass) {
        for (std::uint32_t position = 0U; position < kFpVariantCount;
             ++position) {
          const std::uint32_t variant =
              pass == 2U ? kFpVariantCount - 1U - position : position;
          if (!context_.IsFpVariantAvailable(test, variant)) {
            continue;
          }
          if (stop_requested_.load(std::memory_order_acquire)) {
            SetStopped(run_id, run_start);
            return false;
          }
          const BatchResult candidate = context_.ExecuteBatch(
              test, kFpAutotuneRepetitions, ring_index++, variant);
          if (!candidate.ok) {
            SetError(run_id, kErrorSubmission, candidate.error, run_start);
            return false;
          }
          if (pass != 0U) {
            const double candidate_amount = WorkAmount(
                test, kFpAutotuneRepetitions,
                context_.memory_buffer_bytes(), kFpAccumulatorCounts[variant]);
            accumulated_throughput[variant] +=
                candidate_amount / candidate.seconds;
            ++sample_counts[variant];
          }
        }
      }
      double best_throughput = -1.0;
      for (std::uint32_t variant = 0U; variant < kFpVariantCount; ++variant) {
        if (sample_counts[variant] == 0U) {
          continue;
        }
        const double average = accumulated_throughput[variant] /
                               static_cast<double>(sample_counts[variant]);
        if (average > best_throughput) {
          best_throughput = average;
          selected_fp_variant = variant;
        }
      }
      fp_accumulators = kFpAccumulatorCounts[selected_fp_variant];
    }
    const auto warmup_start = Clock::now();
    do {
      if (stop_requested_.load(std::memory_order_acquire)) {
        SetStopped(run_id, run_start);
        return false;
      }
      const BatchResult warmup =
          context_.ExecuteBatch(test, repetitions, ring_index++,
                                selected_fp_variant);
      if (!warmup.ok) {
        SetError(run_id, kErrorSubmission, warmup.error, run_start);
        return false;
      }
      const double scale = kTargetBatchSeconds / warmup.seconds;
      const auto calibrated = static_cast<std::uint32_t>(std::llround(
          std::clamp(static_cast<double>(repetitions) * scale, 1.0,
                     static_cast<double>(kMaximumDispatchesPerBatch))));
      repetitions = std::clamp(calibrated, 1U, kMaximumDispatchesPerBatch);
    } while (Clock::now() - warmup_start <
             std::chrono::milliseconds(request.warmup_ms));

    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      if (snapshot_.run_id != run_id) {
        return false;
      }
      snapshot_.state = GpuState::kRunning;
      snapshot_.dispatch_count = repetitions;
    }
    const auto measure_start = Clock::now();
    double total_amount = 0.0;
    double total_seconds = 0.0;
    while (Clock::now() - measure_start <
           std::chrono::milliseconds(request.duration_ms)) {
      if (stop_requested_.load(std::memory_order_acquire)) {
        SetStopped(run_id, run_start);
        return false;
      }
      const BatchResult batch =
          context_.ExecuteBatch(test, repetitions, ring_index++,
                                selected_fp_variant);
      if (!batch.ok) {
        SetError(run_id, kErrorSubmission, batch.error, run_start);
        return false;
      }
      total_seconds += batch.seconds;
      total_amount += WorkAmount(test, repetitions,
                                 context_.memory_buffer_bytes(), fp_accumulators);
      const double value = total_amount / total_seconds / 1.0e9;
      const double test_progress = std::clamp(
          std::chrono::duration<double, std::milli>(Clock::now() -
                                                    measure_start)
                  .count() /
              static_cast<double>(request.duration_ms),
          0.0, 1.0);
      {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (snapshot_.run_id != run_id) {
          return false;
        }
        AssignValue(test, value);
        snapshot_.timing_mode = batch.timing_mode;
        snapshot_.gpu_batch_ms = batch.seconds * 1000.0;
        snapshot_.dispatch_count = repetitions;
        snapshot_.progress =
            (static_cast<double>(sequence_index) + test_progress) /
            static_cast<double>(sequence_count);
        snapshot_.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - run_start)
                .count());
      }
    }
    if (!context_.ValidateSink(test)) {
      SetError(run_id, kErrorInvalidResult,
               "GPU shader sink validation failed", run_start);
      return false;
    }
    return true;
  }

  void AssignValue(GpuTest test, double value) {
    switch (test) {
    case GpuTest::kFp32:
      snapshot_.fp32_gflops = value;
      break;
    case GpuTest::kFp16:
      snapshot_.fp16_gflops = value;
      if (snapshot_.fp16_mode == GpuFp16Mode::kNative &&
          snapshot_.fp32_gflops > 0.0) {
        snapshot_.fp16_scaling = value / snapshot_.fp32_gflops;
      } else {
        snapshot_.fp16_scaling = 0.0;
      }
      break;
    case GpuTest::kInt32:
      snapshot_.int32_gops = value;
      break;
    case GpuTest::kMixed:
      snapshot_.mixed_gwork = value;
      break;
    case GpuTest::kMemoryBandwidth:
      snapshot_.memory_bandwidth_gbps = value;
      break;
    default:
      break;
    }
  }

  void SetStopped(std::uint64_t run_id, Clock::time_point run_start) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.state = GpuState::kStopped;
    snapshot_.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             run_start)
            .count());
  }

  void SetError(std::uint64_t run_id, std::int32_t code,
                const std::string &message, Clock::time_point run_start) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (snapshot_.run_id != run_id) {
      return;
    }
    snapshot_.state = GpuState::kError;
    snapshot_.error_code = code;
    snapshot_.last_error = message;
    snapshot_.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                             run_start)
            .count());
  }

  VulkanComputeContext context_{};
  mutable std::mutex snapshot_mutex_{};
  std::mutex lifecycle_mutex_{};
  std::atomic<bool> stop_requested_{false};
  std::thread worker_{};
  GpuSnapshot snapshot_{};
  std::uint64_t next_run_id_ = 1U;
  bool context_available_ = false;
  std::string initialization_error_{};
  std::int32_t initialization_error_code_ = 0;
};

GpuBenchmarkEngine::GpuBenchmarkEngine() : impl_(std::make_unique<Impl>()) {}

GpuBenchmarkEngine::~GpuBenchmarkEngine() = default;

std::int32_t GpuBenchmarkEngine::Start(const GpuRequest &request,
                                       std::uint64_t *out_run_id) {
  return impl_->Start(request, out_run_id);
}

std::int32_t GpuBenchmarkEngine::RequestStop(std::uint64_t run_id) {
  return impl_->RequestStop(run_id);
}

GpuSnapshot GpuBenchmarkEngine::GetSnapshot() const {
  return impl_->GetSnapshot();
}

std::string GpuBenchmarkEngine::GetCapabilitiesJson() const {
  return impl_->GetCapabilitiesJson();
}

} // namespace benchmark
