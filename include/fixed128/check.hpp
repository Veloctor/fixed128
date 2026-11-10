#pragma once
#include "config.hpp"
#include "types.hpp"

#include <cstdio>
#include <cstdlib>

// Input checks of the conversions from double, enabled by FIXED128_CHECK_INPUTS (see config.hpp).
// With FIXED128_CHECK_INPUTS=0 the FIXED128_CHECK_* macros expand to nothing: no runtime cost at all.

namespace fx128
{
	// Called for every invalid input lane: function name, problem ("NaN", "infinite", "out of range"), input value.
	// The default prints the problem and aborts. Replace it to log, break into a debugger or throw (the tests throw).
	// If it returns, the conversion continues and gives an undefined result for that lane.
	using input_error_handler_t = void (*)(const char* function, const char* problem, double input);

	inline void default_input_error_handler(const char* function, const char* problem, double input)
	{
		std::fprintf(stderr, "fixed128: %s: %s input %a\n", function, problem, input);
		std::abort();
	}

	inline input_error_handler_t input_error_handler = default_input_error_handler;

	namespace detail
	{
		// Valid inputs: lo <= v < hi (so -0.0 is valid when lo is 0).
		inline void check_input(const char* function, double v, double lo, double hi)
		{
			if (v != v)
				input_error_handler(function, "NaN", v);
			else if (v - v != 0.0)
				input_error_handler(function, "infinite", v);
			else if (!(v >= lo && v < hi))
				input_error_handler(function, "out of range", v);
		}

		inline void check_input(const char* function, const double4& v, double lo, double hi)
		{
			check_input(function, v.x, lo, hi);
			check_input(function, v.y, lo, hi);
			check_input(function, v.z, lo, hi);
			check_input(function, v.w, lo, hi);
		}
	}
}

#if FIXED128_CHECK_INPUTS
#define FIXED128_CHECK_INPUT(function, v, lo, hi) ::fx128::detail::check_input(function, v, lo, hi)
#else
#define FIXED128_CHECK_INPUT(function, v, lo, hi) ((void)0)
#endif
