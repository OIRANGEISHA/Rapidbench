set(BENCHMARK_CORE_SOURCES
  src/cpu_workload.cpp
  src/device_cpu_isa.cpp
  src/engine.cpp
  src/gpu_timing.cpp
  src/memory_engine.cpp
  src/memory_frequency.cpp
  src/memory_kernels.cpp
  src/performance_hint.cpp
  src/storage_engine.cpp
  src/topology.cpp
)

add_library(rapidbench_sqlite STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite/sqlite3.c
)
target_include_directories(rapidbench_sqlite PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite
)
target_compile_definitions(rapidbench_sqlite PRIVATE
  SQLITE_DQS=0
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_DEPRECATED
  SQLITE_THREADSAFE=1
  SQLITE_USE_URI=0
)
set_target_properties(rapidbench_sqlite PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

if(ANDROID)
  list(APPEND BENCHMARK_CORE_SOURCES
    src/device_vulkan.cpp
    src/gpu_benchmark.cpp
  )
else()
  list(APPEND BENCHMARK_CORE_SOURCES
    src/device_vulkan_stub.cpp
    src/gpu_benchmark_stub.cpp
  )
endif()

add_library(benchmark_core STATIC ${BENCHMARK_CORE_SOURCES})

target_include_directories(benchmark_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(benchmark_core PUBLIC cxx_std_17)
target_link_libraries(benchmark_core PUBLIC
  Threads::Threads
  ${CMAKE_DL_LIBS}
  rapidbench_sqlite
)
if(ANDROID)
  target_link_libraries(benchmark_core PUBLIC log)

  set(RAPIDBENCH_HOST_EXECUTABLE_SUFFIX "")
  if(CMAKE_HOST_WIN32)
    set(RAPIDBENCH_HOST_EXECUTABLE_SUFFIX ".exe")
  endif()
  set(RAPIDBENCH_GLSLC
    "${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}/glslc${RAPIDBENCH_HOST_EXECUTABLE_SUFFIX}"
  )
  if(NOT EXISTS "${RAPIDBENCH_GLSLC}")
    message(FATAL_ERROR "NDK glslc was not found at ${RAPIDBENCH_GLSLC}")
  endif()
  set(RAPIDBENCH_GENERATED_SHADER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gpu_shaders"
  )
  file(MAKE_DIRECTORY "${RAPIDBENCH_GENERATED_SHADER_DIR}")

  set(RAPIDBENCH_GPU_SHADER_NAMES
    fp32_compute
    fp32_compute_12
    fp32_compute_16
    fp16_native
    fp16_native_12
    fp16_native_16
    fp16_emulated
    fp16_emulated_12
    fp16_emulated_16
    int32_compute
    mixed_compute
    memory_bandwidth
  )
  set(RAPIDBENCH_GPU_SHADER_VARIABLES
    kFp32Compute
    kFp32Compute12
    kFp32Compute16
    kFp16Native
    kFp16Native12
    kFp16Native16
    kFp16Emulated
    kFp16Emulated12
    kFp16Emulated16
    kInt32Compute
    kMixedCompute
    kMemoryBandwidth
  )
  set(RAPIDBENCH_GENERATED_SHADER_HEADERS)
  list(LENGTH RAPIDBENCH_GPU_SHADER_NAMES RAPIDBENCH_GPU_SHADER_COUNT)
  math(EXPR RAPIDBENCH_GPU_SHADER_LAST "${RAPIDBENCH_GPU_SHADER_COUNT} - 1")
  foreach(SHADER_INDEX RANGE ${RAPIDBENCH_GPU_SHADER_LAST})
    list(GET RAPIDBENCH_GPU_SHADER_NAMES ${SHADER_INDEX} SHADER_NAME)
    list(GET RAPIDBENCH_GPU_SHADER_VARIABLES ${SHADER_INDEX} SHADER_VARIABLE)
    set(SHADER_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/../gpu/shaders/${SHADER_NAME}.comp"
    )
    set(SHADER_SPV
      "${RAPIDBENCH_GENERATED_SHADER_DIR}/${SHADER_NAME}.spv"
    )
    set(SHADER_HEADER
      "${RAPIDBENCH_GENERATED_SHADER_DIR}/${SHADER_NAME}.spv.h"
    )
    add_custom_command(
      OUTPUT "${SHADER_SPV}" "${SHADER_HEADER}"
      COMMAND "${RAPIDBENCH_GLSLC}" --target-env=vulkan1.0 -O
              "${SHADER_SOURCE}" -o "${SHADER_SPV}"
      COMMAND "${CMAKE_COMMAND}"
              -DINPUT=${SHADER_SPV}
              -DOUTPUT=${SHADER_HEADER}
              -DVARIABLE=${SHADER_VARIABLE}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_spirv.cmake"
      DEPENDS "${SHADER_SOURCE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_spirv.cmake"
      VERBATIM
    )
    list(APPEND RAPIDBENCH_GENERATED_SHADER_HEADERS "${SHADER_HEADER}")
  endforeach()
  add_custom_target(rapidbench_gpu_shaders
    DEPENDS ${RAPIDBENCH_GENERATED_SHADER_HEADERS}
  )
  add_dependencies(benchmark_core rapidbench_gpu_shaders)
  target_include_directories(benchmark_core PRIVATE
    "${RAPIDBENCH_GENERATED_SHADER_DIR}"
  )
endif()

if(MSVC)
  target_compile_options(benchmark_core PRIVATE /O2 /W4)
else()
  target_compile_options(benchmark_core PRIVATE
    -O3
    -Wall
    -Wextra
    -Wpedantic
    -ffp-contract=fast
  )
  set_source_files_properties(src/memory_kernels.cpp PROPERTIES
    COMPILE_OPTIONS "-fno-builtin-memcpy;-fno-builtin-memmove"
  )
endif()

set_target_properties(benchmark_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
