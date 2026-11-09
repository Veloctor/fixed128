add_executable(fixed128_bench bench_conversions.cpp)
target_link_libraries(fixed128_bench PRIVATE fixed128::fixed128)
target_include_directories(fixed128_bench PRIVATE ${PROJECT_SOURCE_DIR}/third_party/nanobench)
