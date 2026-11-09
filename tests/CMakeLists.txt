# Each configuration of the library is its own test executable, because the configuration macros
# must be identical in every translation unit of a program.

set(FIXED128_TEST_SOURCES
	main.cpp
	test_arithmetic.cpp
	test_fixed128_convert.cpp
	test_int64_convert.cpp
	test_types.cpp)

function(fixed128_add_test_config name frac_bits round_half_away)
	set(target fixed128_tests_${name})
	add_executable(${target} ${FIXED128_TEST_SOURCES})
	# Use the library's include directories and flags, but not its configuration definitions.
	target_include_directories(${target} PRIVATE
		$<TARGET_PROPERTY:fixed128,INTERFACE_INCLUDE_DIRECTORIES>
		${PROJECT_SOURCE_DIR}/third_party)
	target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:fixed128,INTERFACE_COMPILE_OPTIONS>)
	target_compile_definitions(${target} PRIVATE
		FIXED128_FRAC_BITS=${frac_bits}
		FIXED128_ROUND_HALF_AWAY_FROM_ZERO=${round_half_away})
	target_compile_features(${target} PRIVATE cxx_std_17)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
	endif()
	add_test(NAME ${target} COMMAND ${target})
endfunction()

fixed128_add_test_config(frac36_round 36 1)
fixed128_add_test_config(frac36_trunc 36 0)
fixed128_add_test_config(frac0_round 0 1)
