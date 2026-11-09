#pragma once
// Reference implementation: exact, pure-integer conversions used as the single source of truth in the tests.
// Deliberately written without any floating-point arithmetic (except building the final double from an exact
// 53-bit integer and a power of two), so it shares no technique with the code under test.

#include <cstdint>
#include <cstring>

namespace ref
{
	struct u128
	{
		uint64_t hi = 0, lo = 0;

		friend bool operator==(u128 a, u128 b) { return a.hi == b.hi && a.lo == b.lo; }
		friend bool operator!=(u128 a, u128 b) { return !(a == b); }
	};

	inline u128 add(u128 a, u128 b)
	{
		u128 r;
		r.lo = a.lo + b.lo;
		r.hi = a.hi + b.hi + (r.lo < a.lo);
		return r;
	}

	inline u128 neg(u128 a)
	{
		u128 r;
		r.lo = ~a.lo + 1;
		r.hi = ~a.hi + (r.lo == 0);
		return r;
	}

	inline u128 sub(u128 a, u128 b) { return add(a, neg(b)); }

	inline bool is_zero(u128 a) { return a.hi == 0 && a.lo == 0; }

	// a << s, s in [0, 127]; bits shifted out are dropped
	inline u128 shl(u128 a, int s)
	{
		if (s == 0) return a;
		if (s >= 64) return {a.lo << (s - 64), 0};
		return {(a.hi << s) | (a.lo >> (64 - s)), a.lo << s};
	}

	// a >> s (logical), s in [0, 127]
	inline u128 shr(u128 a, int s)
	{
		if (s == 0) return a;
		if (s >= 64) return {0, a.hi >> (s - 64)};
		return {a.hi >> s, (a.lo >> s) | (a.hi << (64 - s))};
	}

	inline bool bit(u128 a, int i) { return i < 64 ? (a.lo >> i) & 1 : (a.hi >> (i - 64)) & 1; }

	// Index of the most significant set bit; a must be non-zero.
	inline int msb(u128 a)
	{
		int p = 127;
		while (!bit(a, p)) --p;
		return p;
	}

	inline uint64_t bits_of(double d)
	{
		uint64_t b;
		std::memcpy(&b, &d, sizeof b);
		return b;
	}

	inline double double_of(uint64_t b)
	{
		double d;
		std::memcpy(&d, &b, sizeof d);
		return d;
	}

	// Exact 2^e for e in the normal range of double, built from the bit pattern.
	inline double pow2(int e) { return double_of(static_cast<uint64_t>(e + 1023) << 52); }

	enum class rounding { trunc, half_away_from_zero };

	// Integer magnitude of |v| * 2^frac_bits, rounded as requested. v must be finite and the result must fit in 128 bits.
	inline u128 scaled_magnitude(double v, int frac_bits, rounding mode)
	{
		const uint64_t b = bits_of(v);
		const int biased_exp = static_cast<int>((b >> 52) & 0x7FF);
		uint64_t mant = b & 0xFFFFFFFFFFFFFull;
		int e; // |v| == mant * 2^e
		if (biased_exp == 0) {
			e = -1074;
		}
		else {
			mant |= 1ull << 52;
			e = biased_exp - 1075;
		}
		if (mant == 0) return {};
		const int k = e + frac_bits; // |v| * 2^frac_bits == mant * 2^k
		if (k >= 0) return shl({0, mant}, k);
		const int s = -k;
		uint64_t q = s >= 64 ? 0 : mant >> s;
		if (mode == rounding::half_away_from_zero && s - 1 < 64 && ((mant >> (s - 1)) & 1))
			++q; // the dropped part is >= 1/2
		return {0, q};
	}

	// Two's complement fixed128 (or ufixed128 for non-negative v) of v.
	inline u128 double_to_fixed128(double v, int frac_bits, rounding mode)
	{
		u128 m = scaled_magnitude(v, frac_bits, mode);
		return (bits_of(v) >> 63) ? neg(m) : m;
	}

	// Correctly rounded (to nearest, ties to even) double of x * 2^-frac_bits,
	// x interpreted as signed two's complement if is_signed, unsigned otherwise.
	inline double fixed128_to_double(u128 x, int frac_bits, bool is_signed)
	{
		const bool negative = is_signed && (x.hi >> 63);
		u128 m = negative ? neg(x) : x;
		if (is_zero(m)) return 0.0;
		const int p = msb(m);
		uint64_t top;
		int e;
		if (p <= 52) {
			top = m.lo;
			e = 0;
		}
		else {
			const int s = p - 52;
			top = shr(m, s).lo;
			const bool half = bit(m, s - 1);
			const bool sticky = s - 1 > 0 && !is_zero(shl(m, 128 - (s - 1)));
			if (half && (sticky || (top & 1))) ++top;
			e = s;
			if (top == (1ull << 53)) {
				top >>= 1;
				++e;
			}
		}
		// top < 2^53 is exact as a double, and multiplying by a power of two is exact in the normal range.
		const double r = static_cast<double>(top) * pow2(e - frac_bits);
		return negative ? -r : r;
	}

	// Signed/unsigned int64 truncation of v (v must be in range).
	inline uint64_t double_to_int64_trunc(double v)
	{
		u128 m = scaled_magnitude(v, 0, rounding::trunc);
		return (bits_of(v) >> 63) ? (0 - m.lo) : m.lo;
	}

	// Distance in units in the last place between two finite doubles of the same sign (or zero).
	inline uint64_t ulp_distance(double a, double b)
	{
		auto ord = [](double d) {
			int64_t i = static_cast<int64_t>(bits_of(d));
			return i < 0 ? INT64_MIN - i : i;
		};
		const int64_t ia = ord(a), ib = ord(b);
		return ia > ib ? static_cast<uint64_t>(ia) - static_cast<uint64_t>(ib)
					   : static_cast<uint64_t>(ib) - static_cast<uint64_t>(ia);
	}
}
