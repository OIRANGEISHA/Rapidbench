if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED VARIABLE)
  message(FATAL_ERROR "INPUT, OUTPUT and VARIABLE are required")
endif()

file(READ "${INPUT}" SPIRV_HEX HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," SPIRV_BYTES "${SPIRV_HEX}")
file(WRITE "${OUTPUT}"
  "#pragma once\n#include <cstddef>\n#include <cstdint>\n"
  "namespace benchmark::gpu_shaders {\n"
  "alignas(4) inline constexpr std::uint8_t ${VARIABLE}[] = {${SPIRV_BYTES}};\n"
  "inline constexpr std::size_t ${VARIABLE}Size = sizeof(${VARIABLE});\n"
  "} // namespace benchmark::gpu_shaders\n")
