#pragma once
// 4-lane double-double positions for the benchmark scenarios: the usual alternative to fixed-point positions (same
// 16 bytes per value, about 106 significant bits relative to the magnitude). Written with the library's vector
// primitives, so it is vectorized exactly like fixed128x4 (AVX2, or the portable backend).
// Not part of the library. The algorithms are the classic error-free transformations (Dekker, Knuth) as in QD.
#include <fixed128/fixed128.hpp>

namespace dd4
{
	using namespace fx128::detail;

	struct value
	{
		fx128::double4 hi, lo;
	};

	struct pair
	{
		vd hi, lo;
	};

	inline pair two_sum(vd a, vd b)
	{
		const vd s = add_d(a, b), bb = sub_d(s, a);
		return {s, add_d(sub_d(a, sub_d(s, bb)), sub_d(b, bb))};
	}

	inline pair quick_two_sum(vd a, vd b) // requires |a| >= |b|
	{
		const vd s = add_d(a, b);
		return {s, sub_d(b, sub_d(s, a))};
	}

	inline value from_double4(const fx128::double4& hi) { return {hi, fx128::double4{0, 0, 0, 0}}; }

	// a - b rounded to double, accurate version (IEEE-style error bound; the same steps as QD's ieee_add).
	inline vd subtract_to_double(const value& a, const value& b)
	{
		const vd zero = zero_d();
		const pair s = two_sum(load(a.hi), sub_d(zero, load(b.hi)));
		const pair t = two_sum(load(a.lo), sub_d(zero, load(b.lo)));
		pair u = quick_two_sum(s.hi, add_d(s.lo, t.hi));
		u = quick_two_sum(u.hi, add_d(u.lo, t.lo));
		return add_d(u.hi, u.lo);
	}

	// a - b rounded to double, cheap version: (a.hi - b.hi) + (a.lo - b.lo). Accurate when a and b are close
	// (then a.hi - b.hi is exact), which is the camera-relative case.
	inline vd subtract_to_double_sloppy(const value& a, const value& b)
	{
		return add_d(sub_d(load(a.hi), load(b.hi)), sub_d(load(a.lo), load(b.lo)));
	}

	// a += d (double-double plus double).
	inline void add(value& a, const fx128::double4& d)
	{
		pair s = two_sum(load(a.hi), load(d));
		s = quick_two_sum(s.hi, add_d(s.lo, load(a.lo)));
		a.hi = to_double4(s.hi);
		a.lo = to_double4(s.lo);
	}
}
