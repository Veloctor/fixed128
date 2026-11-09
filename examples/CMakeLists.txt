foreach(example basic_usage xyz_lane_packing)
	add_executable(fixed128_example_${example} ${example}.cpp)
	target_link_libraries(fixed128_example_${example} PRIVATE fixed128::fixed128)
endforeach()
