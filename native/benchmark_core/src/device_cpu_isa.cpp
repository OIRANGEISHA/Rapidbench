#include "benchmark/device_cpu_isa.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__) && defined(__aarch64__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

namespace benchmark {
namespace {

struct FeatureFlag final {
  unsigned long mask;
  const char *name;
};

struct IsaLevel final {
  std::string name;
  std::string confidence;
  std::string evidence;
  std::string source;
  std::string note;
};

void AppendFeatureNames(std::vector<std::string> *features,
                        unsigned long value, const FeatureFlag *flags,
                        std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) {
    if ((value & flags[index].mask) != 0UL) {
      features->emplace_back(flags[index].name);
    }
  }
}

std::string HexValue(unsigned long value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::uppercase << value;
  return output.str();
}

void AppendJsonString(std::ostringstream *output, const std::string &value) {
  *output << '"';
  for (const char character : value) {
    switch (character) {
    case '"':
      *output << "\\\"";
      break;
    case '\\':
      *output << "\\\\";
      break;
    case '\n':
      *output << "\\n";
      break;
    case '\r':
      *output << "\\r";
      break;
    case '\t':
      *output << "\\t";
      break;
    default:
      *output << character;
      break;
    }
  }
  *output << '"';
}

#if defined(__linux__) && defined(__aarch64__)
std::string Trim(const std::string &value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

bool IsArchitectureNumber(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  bool has_digit = false;
  bool has_decimal_point = false;
  for (const char character : value) {
    if (character >= '0' && character <= '9') {
      has_digit = true;
      continue;
    }
    if (character == '.' && !has_decimal_point) {
      has_decimal_point = true;
      continue;
    }
    return false;
  }
  return has_digit && value.front() != '.' && value.back() != '.';
}

IsaLevel ReadReportedArmIsaLevel() {
  std::ifstream cpuinfo("/proc/cpuinfo");
  if (!cpuinfo.is_open()) {
    return {"ARMv8-A or newer (exact level unavailable)", "Unavailable",
            "The kernel CPU information file could not be read",
            "/proc/cpuinfo",
            "No exact Arm architecture version was reported. HWCAP feature "
            "bits are intentionally not used to guess an ISA level."};
  }

  std::vector<std::string> reported_versions;
  std::string line;
  while (std::getline(cpuinfo, line)) {
    const std::size_t separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    if (Trim(line.substr(0, separator)) != "CPU architecture") {
      continue;
    }
    const std::string version = Trim(line.substr(separator + 1U));
    if (!version.empty()) {
      reported_versions.push_back(version);
    }
  }

  if (reported_versions.empty()) {
    return {"ARMv8-A or newer (exact level unavailable)", "Unavailable",
            "CPU architecture was not exposed by the kernel",
            "/proc/cpuinfo",
            "No exact Arm architecture version was reported. HWCAP feature "
            "bits are intentionally not used to guess an ISA level."};
  }

  const std::string &reported = reported_versions.front();
  for (const std::string &version : reported_versions) {
    if (version != reported) {
      return {"Mixed/unknown Arm ISA levels", "Kernel-reported values differ",
              "CPU cores reported different architecture values",
              "/proc/cpuinfo",
              "RapidBench will not collapse different per-core architecture "
              "values into a guessed device-wide ISA level."};
    }
  }

  if (!IsArchitectureNumber(reported)) {
    return {"Unknown Arm ISA level", "Unrecognized kernel value",
            "CPU architecture: " + reported, "/proc/cpuinfo",
            "The kernel value is shown as evidence, but is not converted into "
            "a guessed Arm architecture level."};
  }

  if (reported.find('.') != std::string::npos) {
    return {"ARMv" + reported + "-A", "Kernel-reported exact version",
            "CPU architecture: " + reported, "/proc/cpuinfo",
            "The displayed ISA level comes directly from the kernel report."};
  }

  return {"ARMv" + reported + "-A (minor not exposed)",
          "Kernel-reported major version only",
          "CPU architecture: " + reported, "/proc/cpuinfo",
          "The kernel reports only the major architecture version. HWCAP "
          "features are listed separately and are not used to guess a minor "
          "ISA level."};
}
#endif

} // namespace

std::string QueryCpuIsaInfoJson() {
#if defined(__linux__) && defined(__aarch64__)
  const unsigned long hwcap = getauxval(AT_HWCAP);
  const unsigned long hwcap2 = getauxval(AT_HWCAP2);
  static constexpr FeatureFlag kHwcapFeatures[] = {
      {HWCAP_FP, "FP"},           {HWCAP_ASIMD, "ASIMD / NEON"},
      {HWCAP_EVTSTRM, "EVTSTRM"}, {HWCAP_AES, "AES"},
      {HWCAP_PMULL, "PMULL"},     {HWCAP_SHA1, "SHA1"},
      {HWCAP_SHA2, "SHA2"},       {HWCAP_CRC32, "CRC32"},
      {HWCAP_ATOMICS, "LSE"},     {HWCAP_FPHP, "FP16"},
      {HWCAP_ASIMDHP, "ASIMD FP16"},
      {HWCAP_CPUID, "CPUID"},
      {HWCAP_ASIMDRDM, "RDM"},
      {HWCAP_JSCVT, "JSCVT"},
      {HWCAP_FCMA, "FCMA"},
      {HWCAP_LRCPC, "LRCPC"},
      {HWCAP_DCPOP, "DCPOP"},
      {HWCAP_SHA3, "SHA3"},
      {HWCAP_SM3, "SM3"},
      {HWCAP_SM4, "SM4"},
      {HWCAP_ASIMDDP, "DotProd"},
      {HWCAP_SHA512, "SHA512"},
      {HWCAP_SVE, "SVE"},
      {HWCAP_ASIMDFHM, "FHM"},
      {HWCAP_DIT, "DIT"},
      {HWCAP_USCAT, "USCAT"},
      {HWCAP_ILRCPC, "LRCPC2"},
      {HWCAP_FLAGM, "FlagM"},
      {HWCAP_SSBS, "SSBS"},
      {HWCAP_SB, "SB"},
      {HWCAP_PACA, "PAuth address"},
      {HWCAP_PACG, "PAuth generic"},
  };
  static constexpr FeatureFlag kHwcap2Features[] = {
      {HWCAP2_DCPODP, "DCPoDP"},
      {HWCAP2_SVE2, "SVE2"},
      {HWCAP2_SVEAES, "SVE AES"},
      {HWCAP2_SVEPMULL, "SVE PMULL"},
      {HWCAP2_SVEBITPERM, "SVE BitPerm"},
      {HWCAP2_SVESHA3, "SVE SHA3"},
      {HWCAP2_SVESM4, "SVE SM4"},
      {HWCAP2_FLAGM2, "FlagM2"},
      {HWCAP2_FRINT, "FRINTTS"},
      {HWCAP2_SVEI8MM, "SVE I8MM"},
      {HWCAP2_SVEF32MM, "SVE F32MM"},
      {HWCAP2_SVEF64MM, "SVE F64MM"},
      {HWCAP2_SVEBF16, "SVE BF16"},
      {HWCAP2_I8MM, "I8MM"},
      {HWCAP2_BF16, "BF16"},
      {HWCAP2_DGH, "DGH"},
      {HWCAP2_RNG, "RNG"},
      {HWCAP2_BTI, "BTI"},
      {HWCAP2_MTE, "MTE"},
      {HWCAP2_ECV, "ECV"},
      {HWCAP2_AFP, "AFP"},
      {HWCAP2_RPRES, "RPRES"},
      {HWCAP2_MTE3, "MTE3"},
      {HWCAP2_SME, "SME"},
      {HWCAP2_SME_I16I64, "SME I16I64"},
      {HWCAP2_SME_F64F64, "SME F64F64"},
      {HWCAP2_SME_I8I32, "SME I8I32"},
      {HWCAP2_SME_F16F32, "SME F16F32"},
      {HWCAP2_SME_B16F32, "SME BF16F32"},
      {HWCAP2_SME_F32F32, "SME F32F32"},
      {HWCAP2_SME_FA64, "SME FA64"},
      {HWCAP2_WFXT, "WFxT"},
      {HWCAP2_EBF16, "EBF16"},
      {HWCAP2_SVE_EBF16, "SVE EBF16"},
      {HWCAP2_CSSC, "CSSC"},
      {HWCAP2_RPRFM, "RPRFM"},
      {HWCAP2_SVE2P1, "SVE2.1"},
      {HWCAP2_SME2, "SME2"},
      {HWCAP2_SME2P1, "SME2.1"},
      {HWCAP2_SME_I16I32, "SME I16I32"},
      {HWCAP2_SME_BI32I32, "SME BI32I32"},
      {HWCAP2_SME_B16B16, "SME B16B16"},
      {HWCAP2_SME_F16F16, "SME F16F16"},
      {HWCAP2_MOPS, "MOPS"},
      {HWCAP2_HBC, "HBC"},
      {HWCAP2_SVE_B16B16, "SVE B16B16"},
      {HWCAP2_LRCPC3, "LRCPC3"},
      {HWCAP2_LSE128, "LSE128"},
      {HWCAP2_FPMR, "FPMR"},
      {HWCAP2_LUT, "LUT"},
      {HWCAP2_FAMINMAX, "FAMINMAX"},
      {HWCAP2_F8CVT, "FP8 convert"},
      {HWCAP2_F8FMA, "FP8 FMA"},
      {HWCAP2_F8DP4, "FP8 DP4"},
      {HWCAP2_F8DP2, "FP8 DP2"},
      {HWCAP2_F8E4M3, "FP8 E4M3"},
      {HWCAP2_F8E5M2, "FP8 E5M2"},
      {HWCAP2_SME_LUTV2, "SME LUTv2"},
      {HWCAP2_SME_F8F16, "SME FP8F16"},
      {HWCAP2_SME_F8F32, "SME FP8F32"},
      {HWCAP2_SME_SF8FMA, "SME SF8FMA"},
      {HWCAP2_SME_SF8DP4, "SME SF8DP4"},
      {HWCAP2_SME_SF8DP2, "SME SF8DP2"},
      {HWCAP2_POE, "POE"},
  };

  std::vector<std::string> features;
  AppendFeatureNames(&features, hwcap, kHwcapFeatures,
                     sizeof(kHwcapFeatures) / sizeof(kHwcapFeatures[0]));
  AppendFeatureNames(&features, hwcap2, kHwcap2Features,
                     sizeof(kHwcap2Features) / sizeof(kHwcap2Features[0]));
  const IsaLevel level = ReadReportedArmIsaLevel();

  std::ostringstream output;
  output << "{\"status\":\"available\",\"architecture\":\"AArch64 (A64)\",";
  output << "\"architectureLevel\":";
  AppendJsonString(&output, level.name);
  output << ",\"levelConfidence\":";
  AppendJsonString(&output, level.confidence);
  output << ",\"levelEvidence\":";
  AppendJsonString(&output, level.evidence);
  output << ",\"levelSource\":";
  AppendJsonString(&output, level.source);
  output << ",\"levelNote\":";
  AppendJsonString(&output, level.note);
  output << ",\"featureSource\":\"Linux ELF HWCAP / HWCAP2\",";
  output << "\"hwcap\":";
  AppendJsonString(&output, HexValue(hwcap));
  output << ",\"hwcap2\":";
  AppendJsonString(&output, HexValue(hwcap2));
  output << ",\"features\":[";
  for (std::size_t index = 0; index < features.size(); ++index) {
    if (index != 0U) {
      output << ',';
    }
    AppendJsonString(&output, features[index]);
  }
  output << "]}";
  return output.str();
#elif defined(__aarch64__)
  return "{\"status\":\"unavailable\",\"architecture\":\"AArch64 (A64)\",";
         "\"architectureLevel\":\"ARMv8-A or newer\",";
         "\"reason\":\"Linux HWCAP is unavailable on this platform\",";
         "\"features\":[]}";
#else
  return "{\"status\":\"unavailable\",\"architecture\":\"Unsupported\",";
         "\"architectureLevel\":\"Unavailable\",";
         "\"reason\":\"RapidBench currently ships an arm64-v8a Android binary\",";
         "\"features\":[]}";
#endif
}

} // namespace benchmark

