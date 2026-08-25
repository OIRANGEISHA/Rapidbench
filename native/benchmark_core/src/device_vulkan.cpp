#include "benchmark/device_vulkan.h"

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace benchmark {

namespace {

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
      if (character < 0x20U) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned int>(character) << std::dec;
      } else {
        output << character;
      }
      break;
    }
  }
  return output.str();
}

std::string UnavailableJson(const std::string &reason) {
  return "{\"status\":\"unavailable\",\"reason\":\"" + EscapeJson(reason) +
         "\"}";
}

std::string VersionString(std::uint32_t version) {
  return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
         std::to_string(VK_API_VERSION_MINOR(version)) + "." +
         std::to_string(VK_API_VERSION_PATCH(version));
}

std::string HexValue(std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::uppercase << value;
  return output.str();
}

bool HasExtension(const std::set<std::string> &extensions,
                  const char *extension) {
  return extensions.find(extension) != extensions.end();
}

void AppendFeature(std::ostringstream &output, bool *first, const char *name,
                   bool supported) {
  if (!*first) {
    output << ',';
  }
  *first = false;
  output << '"' << name << "\":" << (supported ? "true" : "false");
}

} // namespace

std::string QueryVulkanInfoJson() {
  void *loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  if (loader == nullptr) {
    return UnavailableJson("Vulkan loader is not present");
  }
  const auto get_instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      dlsym(loader, "vkGetInstanceProcAddr"));
  if (get_instance_proc == nullptr) {
    dlclose(loader);
    return UnavailableJson("vkGetInstanceProcAddr is unavailable");
  }
  const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
      get_instance_proc(VK_NULL_HANDLE, "vkCreateInstance"));
  const auto enumerate_instance_extensions =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          get_instance_proc(VK_NULL_HANDLE,
                            "vkEnumerateInstanceExtensionProperties"));
  if (create_instance == nullptr) {
    dlclose(loader);
    return UnavailableJson("vkCreateInstance is unavailable");
  }

  std::uint32_t loader_version = VK_API_VERSION_1_0;
  const auto enumerate_instance_version =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          get_instance_proc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  if (enumerate_instance_version != nullptr) {
    enumerate_instance_version(&loader_version);
  }

  bool has_properties2_extension = false;
  if (enumerate_instance_extensions != nullptr) {
    std::uint32_t count = 0U;
    if (enumerate_instance_extensions(nullptr, &count, nullptr) == VK_SUCCESS &&
        count > 0U) {
      std::vector<VkExtensionProperties> extensions(count);
      if (enumerate_instance_extensions(nullptr, &count, extensions.data()) ==
          VK_SUCCESS) {
        has_properties2_extension = std::any_of(
            extensions.begin(), extensions.end(), [](const auto &extension) {
              return std::string(extension.extensionName) ==
                     VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
            });
      }
    }
  }

  VkApplicationInfo application_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  application_info.pApplicationName = "RapidBench Device Query";
  application_info.applicationVersion = 1U;
  application_info.pEngineName = "RapidBench";
  application_info.engineVersion = 1U;
  application_info.apiVersion =
      std::min(loader_version, static_cast<std::uint32_t>(VK_API_VERSION_1_3));

  const char *enabled_extension =
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
  VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create_info.pApplicationInfo = &application_info;
  if (loader_version < VK_API_VERSION_1_1 && has_properties2_extension) {
    create_info.enabledExtensionCount = 1U;
    create_info.ppEnabledExtensionNames = &enabled_extension;
  }

  VkInstance instance = VK_NULL_HANDLE;
  const VkResult create_result =
      create_instance(&create_info, nullptr, &instance);
  if (create_result != VK_SUCCESS || instance == VK_NULL_HANDLE) {
    dlclose(loader);
    return UnavailableJson("Vulkan instance creation failed");
  }

  const auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
      get_instance_proc(instance, "vkDestroyInstance"));
  const auto enumerate_devices =
      reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
          get_instance_proc(instance, "vkEnumeratePhysicalDevices"));
  const auto get_properties =
      reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
          get_instance_proc(instance, "vkGetPhysicalDeviceProperties"));
  const auto get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
      get_instance_proc(instance, "vkGetPhysicalDeviceFeatures"));
  const auto enumerate_device_extensions =
      reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
          get_instance_proc(instance, "vkEnumerateDeviceExtensionProperties"));
  auto get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
      get_instance_proc(instance, "vkGetPhysicalDeviceFeatures2"));
  if (get_features2 == nullptr) {
    get_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        get_instance_proc(instance, "vkGetPhysicalDeviceFeatures2KHR"));
  }

  if (enumerate_devices == nullptr || get_properties == nullptr ||
      get_features == nullptr || enumerate_device_extensions == nullptr) {
    if (destroy_instance != nullptr) {
      destroy_instance(instance, nullptr);
    }
    dlclose(loader);
    return UnavailableJson("Required Vulkan query functions are unavailable");
  }

  std::uint32_t device_count = 0U;
  if (enumerate_devices(instance, &device_count, nullptr) != VK_SUCCESS ||
      device_count == 0U) {
    if (destroy_instance != nullptr) {
      destroy_instance(instance, nullptr);
    }
    dlclose(loader);
    return UnavailableJson("No Vulkan physical device was reported");
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  if (enumerate_devices(instance, &device_count, devices.data()) !=
      VK_SUCCESS) {
    if (destroy_instance != nullptr) {
      destroy_instance(instance, nullptr);
    }
    dlclose(loader);
    return UnavailableJson("Vulkan physical device enumeration failed");
  }
  const VkPhysicalDevice device = devices.front();

  VkPhysicalDeviceProperties properties{};
  get_properties(device, &properties);
  std::uint32_t extension_count = 0U;
  enumerate_device_extensions(device, nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> extension_properties(extension_count);
  if (extension_count > 0U) {
    enumerate_device_extensions(device, nullptr, &extension_count,
                                extension_properties.data());
  }
  std::set<std::string> extensions;
  for (const auto &extension : extension_properties) {
    extensions.insert(extension.extensionName);
  }

  VkPhysicalDeviceFeatures base_features{};
  get_features(device, &base_features);
  VkPhysicalDeviceFeatures2 features2{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan12Features features12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features features13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceShaderFloat16Int8Features float16_int8{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
  VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
  VkPhysicalDeviceSynchronization2Features synchronization2{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
  VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragment_shading_rate{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
  VkPhysicalDeviceRayQueryFeaturesKHR ray_query{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

  bool uses_features12 = false;
  bool uses_features13 = false;
  void **chain_tail = &features2.pNext;
  const auto append_to_chain = [&chain_tail](auto *feature) {
    *chain_tail = feature;
    chain_tail = &feature->pNext;
  };
  if (properties.apiVersion >= VK_API_VERSION_1_2) {
    append_to_chain(&features12);
    uses_features12 = true;
  } else {
    if (HasExtension(extensions, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) {
      append_to_chain(&float16_int8);
    }
    if (HasExtension(extensions, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)) {
      append_to_chain(&descriptor_indexing);
    }
    if (HasExtension(extensions, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
      append_to_chain(&timeline_semaphore);
    }
    if (HasExtension(extensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
      append_to_chain(&buffer_device_address);
    }
  }
  if (properties.apiVersion >= VK_API_VERSION_1_3) {
    append_to_chain(&features13);
    uses_features13 = true;
  } else {
    if (HasExtension(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
      append_to_chain(&dynamic_rendering);
    }
    if (HasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
      append_to_chain(&synchronization2);
    }
  }
  if (HasExtension(extensions, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) {
    append_to_chain(&fragment_shading_rate);
  }
  if (HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
    append_to_chain(&ray_query);
  }
  if (HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
    append_to_chain(&ray_tracing);
  }
  if (HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
    append_to_chain(&acceleration_structure);
  }
  if (HasExtension(extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
    append_to_chain(&mesh_shader);
  }
  if (get_features2 != nullptr) {
    get_features2(device, &features2);
    base_features = features2.features;
  }

  const bool shader_float16 = uses_features12
                                  ? features12.shaderFloat16 == VK_TRUE
                                  : float16_int8.shaderFloat16 == VK_TRUE;
  const bool shader_int8 = uses_features12 ? features12.shaderInt8 == VK_TRUE
                                           : float16_int8.shaderInt8 == VK_TRUE;
  const bool has_descriptor_indexing =
      uses_features12
          ? features12.descriptorIndexing == VK_TRUE
          : descriptor_indexing.descriptorBindingPartiallyBound == VK_TRUE;
  const bool has_timeline_semaphore =
      uses_features12 ? features12.timelineSemaphore == VK_TRUE
                      : timeline_semaphore.timelineSemaphore == VK_TRUE;
  const bool has_buffer_device_address =
      uses_features12 ? features12.bufferDeviceAddress == VK_TRUE
                      : buffer_device_address.bufferDeviceAddress == VK_TRUE;
  const bool has_dynamic_rendering =
      uses_features13 ? features13.dynamicRendering == VK_TRUE
                      : dynamic_rendering.dynamicRendering == VK_TRUE;
  const bool has_synchronization2 =
      uses_features13 ? features13.synchronization2 == VK_TRUE
                      : synchronization2.synchronization2 == VK_TRUE;

  std::ostringstream output;
  output << "{\"status\":\"available\",\"deviceName\":\""
         << EscapeJson(properties.deviceName) << "\",\"apiVersion\":\""
         << VersionString(properties.apiVersion) << "\",\"driverVersion\":\""
         << properties.driverVersion << " ("
         << HexValue(properties.driverVersion) << ")\",\"vendorId\":\""
         << HexValue(properties.vendorID) << "\",\"deviceId\":\""
         << HexValue(properties.deviceID) << "\",\"features\":{";
  bool first_feature = true;
  AppendFeature(output, &first_feature, "Geometry Shader",
                base_features.geometryShader == VK_TRUE);
  AppendFeature(output, &first_feature, "Tessellation Shader",
                base_features.tessellationShader == VK_TRUE);
  AppendFeature(output, &first_feature, "Sampler Anisotropy",
                base_features.samplerAnisotropy == VK_TRUE);
  AppendFeature(output, &first_feature, "Shader Int64",
                base_features.shaderInt64 == VK_TRUE);
  AppendFeature(output, &first_feature, "Shader Float64",
                base_features.shaderFloat64 == VK_TRUE);
  AppendFeature(output, &first_feature, "Shader Float16", shader_float16);
  AppendFeature(output, &first_feature, "Shader Int8", shader_int8);
  AppendFeature(output, &first_feature, "Descriptor Indexing",
                has_descriptor_indexing);
  AppendFeature(output, &first_feature, "Timeline Semaphore",
                has_timeline_semaphore);
  AppendFeature(output, &first_feature, "Buffer Device Address",
                has_buffer_device_address);
  AppendFeature(output, &first_feature, "Dynamic Rendering",
                has_dynamic_rendering);
  AppendFeature(output, &first_feature, "Synchronization2",
                has_synchronization2);
  AppendFeature(
      output, &first_feature, "Fragment Shading Rate",
      fragment_shading_rate.pipelineFragmentShadingRate == VK_TRUE ||
          fragment_shading_rate.primitiveFragmentShadingRate == VK_TRUE ||
          fragment_shading_rate.attachmentFragmentShadingRate == VK_TRUE);
  AppendFeature(output, &first_feature, "Ray Query",
                ray_query.rayQuery == VK_TRUE);
  AppendFeature(output, &first_feature, "Ray Tracing Pipeline",
                ray_tracing.rayTracingPipeline == VK_TRUE);
  AppendFeature(output, &first_feature, "Acceleration Structure",
                acceleration_structure.accelerationStructure == VK_TRUE);
  AppendFeature(output, &first_feature, "Mesh Shader",
                mesh_shader.meshShader == VK_TRUE);
  output << "},\"extensions\":[";
  bool first_extension = true;
  for (const std::string &extension : extensions) {
    if (!first_extension) {
      output << ',';
    }
    first_extension = false;
    output << '"' << EscapeJson(extension) << '"';
  }
  output << "]}";

  if (destroy_instance != nullptr) {
    destroy_instance(instance, nullptr);
  }
  dlclose(loader);
  return output.str();
}

} // namespace benchmark
