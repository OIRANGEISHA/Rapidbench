#include "benchmark/gpu_compatibility.h"
#include <sstream>
#include <locale>
namespace benchmark::detail {
namespace {
std::string JsonEscape(const std::string &text) {
  std::string escaped;
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { escaped += '\\'; escaped += c; }
    else if (c == '\n') escaped += "\\n";
    else if (c == '\r') escaped += "\\r";
    else if (c == '\t') escaped += "\\t";
    else if (c >= 32) escaped += c;
  }
  return escaped;
}
}
std::string GpuDiagnosticsJson(std::uint64_t run_id,
                                const GpuDiagnostics &items, bool fatal) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "{\"schema\":1,\"method\":\"gpu-throughput-v2\",\"runId\":" << run_id
      << ",\"fatal\":" << (fatal ? "true" : "false") << ",\"tests\":{";
  for (std::size_t i = 1; i < items.size(); ++i) {
    if (i > 1) out << ',';
    const auto &item = items[i];
    out << '"' << i << "\":{\"state\":" << static_cast<unsigned>(item.state)
        << ",\"runId\":" << item.run_id
        << ",\"hostSeconds\":" << item.host_seconds
        << ",\"gpuObservedSeconds\":" << item.gpu_seconds
        << ",\"gpuObservations\":" << item.gpu_observations
        << ",\"hostBatches\":" << item.host_batches
        << ",\"timestampBatches\":" << item.timestamp_batches
        << ",\"fpAccumulators\":" << item.fp_accumulators
        << ",\"reason\":\"" << JsonEscape(item.reason) << "\"}";
  }
  out << "}}";
  return out.str();
}
} // namespace benchmark::detail
