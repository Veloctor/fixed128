# Benchmarks (nanobench); always measure a Release build.
find_package(Git QUIET)
set(_fixed128_describe "unknown")
if(GIT_FOUND)
	execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE _fixed128_describe OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _fixed128_build_type)
set(_fixed128_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_fixed128_build_type}} ${FIXED128_AVX2_FLAG} (${CMAKE_BUILD_TYPE})")

add_executable(fixed128_bench bench_conversions.cpp)
target_link_libraries(fixed128_bench PRIVATE fixed128::fixed128)
target_include_directories(fixed128_bench PRIVATE ${PROJECT_SOURCE_DIR}/third_party/nanobench ${PROJECT_SOURCE_DIR}/experimental)
target_compile_definitions(fixed128_bench PRIVATE
	FIXED128_BENCH_FLAGS="${_fixed128_flags}"
	FIXED128_GIT_DESCRIBE="${_fixed128_describe}")

# Optional: the same benchmarks compiled for the build machine (-march=native), e.g. to get the AVX-512 upper bounds.
# The compiler may then generate different code for the library too; compare with fixed128_bench.
option(FIXED128_BENCH_NATIVE "Also build fixed128_bench_native with -march=native (GCC, Clang, clang-cl)" OFF)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(_fixed128_native_flag /clang:-march=native) # clang-cl; MSVC has no equivalent
else()
	set(_fixed128_native_flag -march=native)
endif()
if(FIXED128_BENCH_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	add_executable(fixed128_bench_native bench_conversions.cpp)
	target_link_libraries(fixed128_bench_native PRIVATE fixed128::fixed128)
	target_include_directories(fixed128_bench_native PRIVATE ${PROJECT_SOURCE_DIR}/third_party/nanobench ${PROJECT_SOURCE_DIR}/experimental)
	target_compile_options(fixed128_bench_native PRIVATE ${_fixed128_native_flag})
	target_compile_definitions(fixed128_bench_native PRIVATE
		FIXED128_BENCH_FLAGS="${_fixed128_flags} -march=native"
		FIXED128_GIT_DESCRIBE="${_fixed128_describe}")
endif()

# Assembly of the conversion kernels, for reading and for static analysis with uiCA / llvm-mca:
# build the target fixed128_kernels; the listings are written next to its object files (see tools/bench/README.md).
add_library(fixed128_kernels OBJECT kernels.cpp)
target_link_libraries(fixed128_kernels PRIVATE fixed128::fixed128)
if(MSVC)
	target_compile_options(fixed128_kernels PRIVATE /FAs /Fa${CMAKE_CURRENT_BINARY_DIR}/kernels.asm)
else()
	target_compile_options(fixed128_kernels PRIVATE -save-temps=obj -fverbose-asm)
endif()
set_target_properties(fixed128_kernels PROPERTIES EXCLUDE_FROM_ALL ON)
