# Each configuration of the library is its own test executable, because the configuration macros (rounding, backend)
# must be identical in every translation unit of a program. The scales (fractional bits) are template parameters
# and all tested in each executable (FIXED128_TEST_FRAC_BITS in test_helpers.hpp).
# Every configuration is built for the scalar backend, and also for the AVX2 backend when that is the library's
# backend; the scalar executables are compiled without AVX2, so x86-64 machines test both backends.

set(FIXED128_TEST_SOURCES
	main.cpp
	test_arithmetic.cpp
	test_backend_determinism.cpp
	test_contract.cpp
	test_input_checks.cpp
	test_properties.cpp
	test_reference.cpp
	test_int64_convert.cpp
	test_types.cpp)

function(fixed128_add_test_config name round_half_away backend)
	string(TOLOWER ${backend} backend_lower)
	set(target fixed128_tests_${name}_${backend_lower})
	add_executable(${target} ${FIXED128_TEST_SOURCES})
	# Use the library's include directories, but not its configuration definitions and flags.
	target_include_directories(${target} PRIVATE
		$<TARGET_PROPERTY:fixed128,INTERFACE_INCLUDE_DIRECTORIES>
		${PROJECT_SOURCE_DIR}/third_party
		${PROJECT_SOURCE_DIR}/experimental)
	target_compile_definitions(${target} PRIVATE
		FIXED128_ROUND_HALF_AWAY_FROM_ZERO=${round_half_away}
		# input checks on in every build type: they must never fire on the (valid) test inputs
		FIXED128_CHECK_INPUTS=1
		FIXED128_BACKEND_${backend}=1)
	if(backend STREQUAL "AVX2")
		target_compile_options(${target} PRIVATE ${FIXED128_AVX2_FLAG})
	else()
		# Also the portable carry code of the scalar fixed128, as on non-x86 architectures.
		target_compile_definitions(${target} PRIVATE FIXED128_HAS_CARRY_INTRINSICS=0)
	endif()
	target_compile_features(${target} PRIVATE cxx_std_17)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
	endif()
	add_test(NAME ${target} COMMAND ${target})
endfunction()

set(_backends SCALAR)
if(FIXED128_RESOLVED_BACKEND STREQUAL "AVX2")
	list(PREPEND _backends AVX2)
endif()
foreach(backend IN LISTS _backends)
	fixed128_add_test_config(round 1 ${backend})
	fixed128_add_test_config(trunc 0 ${backend})
endforeach()

# Accuracy report (not a test: prints statistics), built with the library's configuration.
add_executable(fixed128_accuracy_report accuracy_report.cpp)
target_link_libraries(fixed128_accuracy_report PRIVATE fixed128::fixed128)
target_include_directories(fixed128_accuracy_report PRIVATE ${PROJECT_SOURCE_DIR}/third_party ${PROJECT_SOURCE_DIR}/experimental)
target_compile_definitions(fixed128_accuracy_report PRIVATE DOCTEST_CONFIG_DISABLE)
