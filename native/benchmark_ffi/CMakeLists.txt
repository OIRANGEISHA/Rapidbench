add_library(benchmark_ffi SHARED
  src/benchmark_ffi.cpp
  src/device_ffi.cpp
  src/gpu_ffi.cpp
  src/storage_ffi.cpp
)

target_include_directories(benchmark_ffi
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(benchmark_ffi PRIVATE cxx_std_17)
target_link_libraries(benchmark_ffi PRIVATE benchmark_core)

target_compile_definitions(benchmark_ffi PRIVATE BM_FFI_BUILDING_LIBRARY=1)

if(NOT MSVC)
  target_compile_options(benchmark_ffi PRIVATE
    -O3
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=hidden
  )
endif()

set_target_properties(benchmark_ffi PROPERTIES
  OUTPUT_NAME benchmark_ffi
)
