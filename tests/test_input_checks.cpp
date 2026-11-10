// FIXED128_CHECK_INPUTS: invalid inputs of the conversions from double are reported, valid boundary values are not.
#include "input_error.hpp"
#include "test_helpers.hpp"

#include <limits>
#include <string>

using namespace fx128;

namespace
{
	constexpr double kInf = std::numeric_limits<double>::infinity();
	constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

	// The problem reported for v in lane 2 (the other lanes valid), or "" if none.
	template <class Fn>
	std::string problem_of(double v, Fn&& convert)
	{
		try {
			convert(double4{0.0, 0.0, v, 0.0});
		}
		catch (const input_error& e) {
			return e.problem;
		}
		return "";
	}

	double next_below(double v) { return v > 0 ? ref::double_of(ref::bits_of(v) - 1) : -ref::double_of(ref::bits_of(-v) + 1); }
	double next_above(double v) { return v > 0 ? ref::double_of(ref::bits_of(v) + 1) : -ref::double_of(ref::bits_of(-v) - 1); }
}

TEST_SUITE("input checks")
{
	TEST_CASE("the tests are built with input checks enabled")
	{
		CHECK(FIXED128_CHECK_INPUTS == 1);
	}

	TEST_CASE_TEMPLATE("double_to_fixed128_full: valid range is [-2^127, 2^127) units", F, FIXED128_TEST_FRAC_BITS)
	{
		auto convert = [](const double4& v) { ulong4 hi, lo; double_to_fixed128_full<F::value>(v, hi, lo); };
		const double limit = ref::pow2(127 - F::value);
		CHECK(problem_of(kNaN, convert) == "NaN");
		CHECK(problem_of(kInf, convert) == "infinite");
		CHECK(problem_of(-kInf, convert) == "infinite");
		CHECK(problem_of(limit, convert) == "out of range");
		CHECK(problem_of(next_below(-limit), convert) == "out of range");
		CHECK(problem_of(-limit, convert) == "");
		CHECK(problem_of(next_below(limit), convert) == "");
		CHECK(problem_of(-0.0, convert) == "");
		CHECK(problem_of(ref::double_of(1), convert) == ""); // smallest subnormal
	}

	TEST_CASE_TEMPLATE("double_to_ufixed128_full: valid range is [0, 2^128) units", F, FIXED128_TEST_FRAC_BITS)
	{
		auto convert = [](const double4& v) { ulong4 hi, lo; double_to_ufixed128_full<F::value>(v, hi, lo); };
		const double limit = ref::pow2(128 - F::value);
		CHECK(problem_of(kNaN, convert) == "NaN");
		CHECK(problem_of(kInf, convert) == "infinite");
		CHECK(problem_of(limit, convert) == "out of range");
		CHECK(problem_of(-ref::double_of(1), convert) == "out of range"); // any negative value
		CHECK(problem_of(-1.0, convert) == "out of range");
		CHECK(problem_of(next_below(limit), convert) == "");
		CHECK(problem_of(-0.0, convert) == "");
		CHECK(problem_of(0.0, convert) == "");
	}

	TEST_CASE("double_to_int64_fast / double_to_uint64_fast")
	{
		auto to_int = [](const double4& v) { double_to_int64_fast(v); };
		auto to_uint = [](const double4& v) { double_to_uint64_fast(v); };
		CHECK(problem_of(kNaN, to_int) == "NaN");
		CHECK(problem_of(ref::pow2(63), to_int) == "out of range");
		CHECK(problem_of(-ref::pow2(63), to_int) == "");
		CHECK(problem_of(next_below(ref::pow2(63)), to_int) == "");
		CHECK(problem_of(-kInf, to_uint) == "infinite");
		CHECK(problem_of(-1.0, to_uint) == "out of range");
		CHECK(problem_of(ref::pow2(64), to_uint) == "out of range");
		CHECK(problem_of(next_below(ref::pow2(64)), to_uint) == "");
	}

	TEST_CASE_TEMPLATE("scalar fixed128 / fixed64 constructors and fixed128 += double", F, FIXED128_TEST_FRAC_BITS_64)
	{
		auto make128 = [](const double4& v) { (void)fixed128<F::value>{v.z}; };
		auto add128 = [](const double4& v) { fixed128<F::value> x; x += v.z; };
		auto make64 = [](const double4& v) { (void)fixed64<F::value>{v.z}; };
		const double limit128 = ref::pow2(127 - F::value), limit64 = ref::pow2(63 - F::value);
		CHECK(problem_of(limit128, make128) == "out of range");
		CHECK(problem_of(-limit128, make128) == "");
		CHECK(problem_of(kNaN, add128) == "NaN");
		CHECK(problem_of(limit128, add128) == "out of range");
		CHECK(problem_of(limit64, make64) == "out of range");
		CHECK(problem_of(-limit64, make64) == "");
		CHECK(problem_of(next_above(-limit64), make64) == "");
	}
}
