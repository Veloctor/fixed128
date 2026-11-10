#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "input_error.hpp"

#include <fixed128/check.hpp>

int main(int argc, char** argv)
{
	fx128::input_error_handler = [](const char* function, const char* problem, double input) {
		throw input_error(function, problem, input);
	};
	return doctest::Context(argc, argv).run();
}
