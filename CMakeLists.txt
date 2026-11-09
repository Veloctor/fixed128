cmake_minimum_required(VERSION 3.20)

project(fixed128
	VERSION 0.1.0
	DESCRIPTION "AVX2-accelerated conversions between double and 128-bit fixed-point numbers"
	LANGUAGES CXX)

include(GNUInstallDirs)

# Tests, benchmarks and examples are only built when this is the top-level project,
# not when consumed through add_subdirectory / FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(FIXED128_IS_TOP_LEVEL ON)
else()
	set(FIXED128_IS_TOP_LEVEL OFF)
endif()

set(FIXED128_FRAC_BITS 36 CACHE STRING "Number of fractional bits of the fixed-point types")
option(FIXED128_ROUND_HALF_AWAY_FROM_ZERO "double -> fixed128 rounds half away from zero (OFF: truncate)" ON)
option(FIXED128_BUILD_TESTS "Build the fixed128 tests" ${FIXED128_IS_TOP_LEVEL})
option(FIXED128_BUILD_BENCHMARKS "Build the fixed128 benchmarks" ${FIXED128_IS_TOP_LEVEL})
option(FIXED128_BUILD_EXAMPLES "Build the fixed128 examples" ${FIXED128_IS_TOP_LEVEL})
option(FIXED128_INSTALL "Generate the fixed128 install target" ${FIXED128_IS_TOP_LEVEL})

# ---- Library ----

add_library(fixed128 INTERFACE)
add_library(fixed128::fixed128 ALIAS fixed128)

target_include_directories(fixed128 INTERFACE
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(fixed128 INTERFACE cxx_std_17)

# Every consumer must see the same configuration, otherwise the program violates the ODR.
if(FIXED128_ROUND_HALF_AWAY_FROM_ZERO)
	set(_fixed128_round 1)
else()
	set(_fixed128_round 0)
endif()
target_compile_definitions(fixed128 INTERFACE
	FIXED128_FRAC_BITS=${FIXED128_FRAC_BITS}
	FIXED128_ROUND_HALF_AWAY_FROM_ZERO=${_fixed128_round})

# AVX2 is required; fast-math is forbidden (checked in config.hpp).
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	target_compile_options(fixed128 INTERFACE /arch:AVX2)
else()
	target_compile_options(fixed128 INTERFACE -mavx2)
endif()

# ---- Tests / benchmarks / examples ----

if(FIXED128_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(FIXED128_BUILD_BENCHMARKS)
	add_subdirectory(benchmarks)
endif()

if(FIXED128_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

# ---- Install / find_package support ----

if(FIXED128_INSTALL)
	include(CMakePackageConfigHelpers)

	install(TARGETS fixed128 EXPORT fixed128Targets)
	install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
	install(EXPORT fixed128Targets
		NAMESPACE fixed128::
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fixed128)

	configure_package_config_file(cmake/fixed128Config.cmake.in
		${PROJECT_BINARY_DIR}/fixed128Config.cmake
		INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fixed128)
	write_basic_package_version_file(${PROJECT_BINARY_DIR}/fixed128ConfigVersion.cmake
		COMPATIBILITY SameMinorVersion
		ARCH_INDEPENDENT)
	install(FILES
		${PROJECT_BINARY_DIR}/fixed128Config.cmake
		${PROJECT_BINARY_DIR}/fixed128ConfigVersion.cmake
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fixed128)
endif()
