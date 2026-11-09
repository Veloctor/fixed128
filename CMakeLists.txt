cmake_minimum_required(VERSION 3.20)

project(fixed128
	VERSION 0.1.0
	DESCRIPTION "Fast 4-lane conversions between double and 128-bit fixed-point numbers (AVX2 or portable)"
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
# Backend (see config.hpp). AUTO: AVX2 on x86-64, scalar elsewhere. Resolved here, so that every consumer sees the
# same explicit choice.
set(FIXED128_BACKEND AUTO CACHE STRING "Backend of the x4 types and conversions: AUTO, AVX2 or SCALAR")
set_property(CACHE FIXED128_BACKEND PROPERTY STRINGS AUTO AVX2 SCALAR)
if(FIXED128_BACKEND STREQUAL "AUTO")
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
		set(FIXED128_RESOLVED_BACKEND AVX2)
	else()
		set(FIXED128_RESOLVED_BACKEND SCALAR)
	endif()
elseif(FIXED128_BACKEND STREQUAL "AVX2" OR FIXED128_BACKEND STREQUAL "SCALAR")
	set(FIXED128_RESOLVED_BACKEND ${FIXED128_BACKEND})
else()
	message(FATAL_ERROR "FIXED128_BACKEND must be AUTO, AVX2 or SCALAR, not '${FIXED128_BACKEND}'")
endif()
message(STATUS "fixed128 backend: ${FIXED128_RESOLVED_BACKEND}")
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
	FIXED128_ROUND_HALF_AWAY_FROM_ZERO=${_fixed128_round}
	FIXED128_BACKEND_${FIXED128_RESOLVED_BACKEND}=1)

# The AVX2 backend needs the whole program compiled for AVX2; fast-math is forbidden (both checked in config.hpp).
# FIXED128_AVX2_FLAG is also used by the tests.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(FIXED128_AVX2_FLAG /arch:AVX2)
else()
	set(FIXED128_AVX2_FLAG -mavx2)
endif()
if(FIXED128_RESOLVED_BACKEND STREQUAL "AVX2")
	target_compile_options(fixed128 INTERFACE ${FIXED128_AVX2_FLAG})
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
