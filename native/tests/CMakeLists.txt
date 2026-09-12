add_executable(phase2_smoke_test
  phase2_smoke_test.cpp
)

target_compile_features(phase2_smoke_test PRIVATE cxx_std_17)
target_link_libraries(phase2_smoke_test PRIVATE benchmark_ffi)

add_test(NAME phase2_smoke_test COMMAND phase2_smoke_test)

add_executable(topology_grouping_test
  topology_grouping_test.cpp
)

target_compile_features(topology_grouping_test PRIVATE cxx_std_17)
target_link_libraries(topology_grouping_test PRIVATE benchmark_core)

add_test(NAME topology_grouping_test COMMAND topology_grouping_test)


add_executable(gpu_timing_test
  gpu_timing_test.cpp
)

target_compile_features(gpu_timing_test PRIVATE cxx_std_17)
target_link_libraries(gpu_timing_test PRIVATE benchmark_core)

add_test(NAME gpu_timing_test COMMAND gpu_timing_test)

add_executable(storage_sqlite_test
  storage_sqlite_test.cpp
)

target_compile_features(storage_sqlite_test PRIVATE cxx_std_17)
target_link_libraries(storage_sqlite_test PRIVATE benchmark_ffi)

add_test(NAME storage_sqlite_test COMMAND storage_sqlite_test)

add_executable(memory_timing_test memory_timing_test.cpp)
target_compile_features(memory_timing_test PRIVATE cxx_std_17)
target_link_libraries(memory_timing_test PRIVATE benchmark_core)
add_test(NAME memory_timing_test COMMAND memory_timing_test)

add_executable(storage_routes_test storage_routes_test.cpp)
target_compile_features(storage_routes_test PRIVATE cxx_std_17)
target_include_directories(storage_routes_test PRIVATE ../benchmark_core/include)
target_link_libraries(storage_routes_test PRIVATE benchmark_ffi)
add_test(NAME storage_routes_test COMMAND storage_routes_test)

add_executable(memory_engine_test memory_engine_test.cpp)
target_compile_features(memory_engine_test PRIVATE cxx_std_17)
target_link_libraries(memory_engine_test PRIVATE benchmark_core)
add_test(NAME memory_engine_test COMMAND memory_engine_test)

add_executable(gpu_validation_test gpu_validation_test.cpp)
target_compile_features(gpu_validation_test PRIVATE cxx_std_17)
target_link_libraries(gpu_validation_test PRIVATE benchmark_core)
add_test(NAME gpu_validation_test COMMAND gpu_validation_test)

add_executable(cpu_publication_test cpu_publication_test.cpp)
add_executable(cpu_application_test cpu_application_test.cpp)
target_compile_features(cpu_application_test PRIVATE cxx_std_17)
target_link_libraries(cpu_application_test PRIVATE benchmark_ffi benchmark_core)
add_test(NAME cpu_application_test COMMAND cpu_application_test)
target_compile_features(cpu_publication_test PRIVATE cxx_std_17)
target_link_libraries(cpu_publication_test PRIVATE benchmark_core)
add_test(NAME cpu_publication_test COMMAND cpu_publication_test)

add_test(NAME gpu_shader_contract_test COMMAND ${CMAKE_COMMAND}
  -DSHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../gpu/shaders
  -P ${CMAKE_CURRENT_SOURCE_DIR}/gpu_shader_contract_test.cmake)

if(ANDROID)
  add_executable(gpu_device_smoke gpu_device_smoke.cpp)
  target_compile_features(gpu_device_smoke PRIVATE cxx_std_17)
  target_link_libraries(gpu_device_smoke PRIVATE benchmark_ffi)
  add_test(NAME gpu_device_smoke COMMAND gpu_device_smoke)
endif()

