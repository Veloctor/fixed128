#pragma once
#include <stdexcept>
#include <string>

// Thrown by the tests' fx128::input_error_handler (installed in main.cpp): every test executable is built with
// FIXED128_CHECK_INPUTS=1, so a valid input wrongly reported as invalid fails the test that used it.
struct input_error : std::runtime_error
{
	std::string function, problem;
	double input;

	input_error(const char* function_, const char* problem_, double input_)
		: std::runtime_error(std::string(function_) + ": " + problem_ + " input"), function(function_), problem(problem_), input(input_)
	{
	}
};
