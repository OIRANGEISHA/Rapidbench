cmake_minimum_required(VERSION 3.18)
project(benchmark_native_android LANGUAGES C CXX)

set(BM_BUILD_TESTS OFF CACHE BOOL "Build native smoke tests" FORCE)
add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../native"
  "${CMAKE_CURRENT_BINARY_DIR}/benchmark-native"
)

