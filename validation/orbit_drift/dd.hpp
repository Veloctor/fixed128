#pragma once
// Double-double arithmetic (about 106 significant bits), used for the reference solution of the orbit drift experiment.
// Classic error-free transformations (Dekker, Knuth); requires exact IEEE double arithmetic and a correct std::fma.
#include <cmath>

struct dd
{
	double hi = 0, lo = 0;

	dd() = default;
	dd(double h) : hi(h) {}
	dd(double h, double l) : hi(h), lo(l) {}
	double to_double() const { return hi + lo; }
};

namespace dd_detail
{
	inline dd two_sum(double a, double b)
	{
		const double s = a + b, bb = s - a;
		return {s, (a - (s - bb)) + (b - bb)};
	}

	inline dd quick_two_sum(double a, double b) // requires |a| >= |b|
	{
		const double s = a + b;
		return {s, b - (s - a)};
	}

	inline dd two_prod(double a, double b)
	{
		const double p = a * b;
		return {p, std::fma(a, b, -p)};
	}
}

inline dd operator+(const dd& a, const dd& b)
{
	using namespace dd_detail;
	dd s = two_sum(a.hi, b.hi);
	const dd t = two_sum(a.lo, b.lo);
	s.lo += t.hi;
	s = quick_two_sum(s.hi, s.lo);
	s.lo += t.lo;
	return quick_two_sum(s.hi, s.lo);
}

inline dd operator-(const dd& a) { return {-a.hi, -a.lo}; }
inline dd operator-(const dd& a, const dd& b) { return a + -b; }

inline dd operator*(const dd& a, const dd& b)
{
	using namespace dd_detail;
	dd p = two_prod(a.hi, b.hi);
	p.lo += a.hi * b.lo + a.lo * b.hi;
	return quick_two_sum(p.hi, p.lo);
}

inline dd operator/(const dd& a, const dd& b)
{
	const double q1 = a.hi / b.hi;
	dd r = a - b * dd(q1);
	const double q2 = r.hi / b.hi;
	r = r - b * dd(q2);
	const double q3 = r.hi / b.hi;
	return dd_detail::quick_two_sum(q1, q2) + dd(q3);
}

inline dd sqrt(const dd& a)
{
	if (a.hi <= 0) return {0, 0};
	const double q = std::sqrt(a.hi);
	const dd r = a - dd_detail::two_prod(q, q);
	return dd_detail::quick_two_sum(q, r.hi / (2 * q)) + dd(0); // one Newton step: q + (a - q^2) / (2q)
}

inline dd& operator+=(dd& a, const dd& b) { return a = a + b; }
inline dd& operator-=(dd& a, const dd& b) { return a = a - b; }

inline dd abs(const dd& a) { return a.hi < 0 ? -a : a; }
