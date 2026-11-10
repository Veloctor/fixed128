# Long-running validation experiments (not unit tests). Each builds against the library headers with an explicit
# rounding configuration, like the tests, so both roundings can be compared in one build.
function(fixed128_add_orbit_drift target round_half_away)
	add_executable(${target} orbit_drift/main.cpp)
	target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:fixed128,INTERFACE_INCLUDE_DIRECTORIES>)
	target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:fixed128,INTERFACE_COMPILE_OPTIONS>)
	target_compile_definitions(${target} PRIVATE
		FIXED128_ROUND_HALF_AWAY_FROM_ZERO=${round_half_away}
		FIXED128_BACKEND_${FIXED128_RESOLVED_BACKEND}=1)
	target_compile_features(${target} PRIVATE cxx_std_17)
endfunction()

fixed128_add_orbit_drift(fixed128_orbit_drift 1)
fixed128_add_orbit_drift(fixed128_orbit_drift_trunc 0)

if(FIXED128_BUILD_TESTS)
	# Smoke test: a short integration far from the origin, where fixed128 must beat double positions.
	add_test(NAME fixed128_orbit_drift_smoke COMMAND fixed128_orbit_drift --years 0.05 --offset 1e21 --check)
endif()
