#pragma once
#include "check.hpp"
#include "config.hpp"
#include "detail/pow2.hpp"

#include <climits>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ostream>

// x86-64 uses the carry intrinsics; defining FIXED128_HAS_CARRY_INTRINSICS=0 selects the portable code (used by the tests).
#ifndef FIXED128_HAS_CARRY_INTRINSICS
#if defined(_M_X64) || defined(__x86_64__)
#define FIXED128_HAS_CARRY_INTRINSICS 1
#else
#define FIXED128_HAS_CARRY_INTRINSICS 0
#endif
#endif
#if FIXED128_HAS_CARRY_INTRINSICS
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

// Scalar fixed-point types: fixed64<FracBits> and fixed128<FracBits>.

namespace fx128
{
	namespace detail
	{
		// *out = a + b + carry, returns the carry out. Uses the x86-64 carry intrinsics where available.
		inline unsigned char addcarry(unsigned char carry, uint64_t a, uint64_t b, uint64_t* out)
		{
#if FIXED128_HAS_CARRY_INTRINSICS
			// The intrinsics take unsigned long long*, which differs from uint64_t (unsigned long) on LP64 platforms.
			unsigned long long r;
			carry = _addcarry_u64(carry, a, b, &r);
			*out = r;
			return carry;
#else
			const uint64_t s = a + b;
			const uint64_t r = s + carry;
			*out = r;
			return static_cast<unsigned char>((s < a) | (r < s));
#endif
		}

		// *out = a - b - borrow, returns the borrow out.
		inline unsigned char subborrow(unsigned char borrow, uint64_t a, uint64_t b, uint64_t* out)
		{
#if FIXED128_HAS_CARRY_INTRINSICS
			unsigned long long r;
			borrow = _subborrow_u64(borrow, a, b, &r);
			*out = r;
			return borrow;
#else
			const uint64_t d = a - b;
			const uint64_t r = d - borrow;
			*out = r;
			return static_cast<unsigned char>((a < b) | (d < borrow));
#endif
		}
	}

	constexpr double ULongMaxValueSD = static_cast<double>(ULLONG_MAX); // == 2^64

	template<int FracBits>
	struct fixed64
	{
		static constexpr int frac_bits = FracBits;

		union
		{
			int64_t sbits;
			uint64_t ubits;
		};

		explicit fixed64(double fp)
		{
			FIXED128_CHECK_INPUT("fixed64(double)", fp, -detail::pow2(63 - FracBits), detail::pow2(63 - FracBits));
			sbits = static_cast<int64_t>(fp * std::pow(2.0, FracBits));
		}

		explicit operator double() const
		{
			return sbits * std::pow(2.0, -FracBits);
		}

		explicit operator int64_t() const
		{
			return sbits;
		}

		explicit operator uint64_t() const
		{
			return ubits;
		}

		bool operator==(const fixed64& rhs) const { return sbits == rhs.sbits; }

		bool operator!=(const fixed64& rhs) const
		{
			return sbits != rhs.sbits;
		}

		bool operator<(const fixed64& rhs) const
		{
			return sbits < rhs.sbits;
		}

		bool operator>(const fixed64& rhs) const
		{
			return sbits > rhs.sbits;
		}
	};

	// Largest / smallest representable fixed64, set bitwise (converting from double would overflow).
	template<int FracBits>
	const fixed64<FracBits> fixed64_maxvalue = [] { fixed64<FracBits> f{0.0}; f.sbits = LLONG_MAX; return f; }();
	template<int FracBits>
	const fixed64<FracBits> fixed64_minvalue = [] { fixed64<FracBits> f{0.0}; f.sbits = LLONG_MIN; return f; }();

	template<int FracBits>
	struct fixed128
	{
		static constexpr int frac_bits = FracBits;

		uint64_t upper, lower;

		// truncates towards zero
		explicit fixed128(double fp)
		{
			FIXED128_CHECK_INPUT("fixed128(double)", fp, -detail::pow2(127 - FracBits), detail::pow2(127 - FracBits));
			fp *= std::pow(2.0, FracBits);
			double abs = std::abs(fp);
			double div = abs * (1.0 / ULongMaxValueSD);
			double c1trunc = std::trunc(div); //maybe trunc better?
			uint64_t c1 = static_cast<uint64_t>(div);
			uint64_t c0 = static_cast<uint64_t>(std::fma(c1trunc, -ULongMaxValueSD, abs));
			if (fp < 0.0) {
				c1 = 0 - c1;
				c1 -= c0 > 0;
				c0 = 0 - c0;
			}
			upper = c1;
			lower = c0;
		}

		//warn: no value shift, direct bit assign
		explicit fixed128(int64_t _upper, int64_t _lower)
		{
			upper = _upper;
			lower = _lower;
		}

		explicit fixed128(fixed64<FracBits> _lower)
		{
			upper = 0;
			lower = _lower.ubits;
		}

		fixed128()
		{
			upper = 0;
			lower = 0;
		}

		bool is_negative() const
		{
			return upper >> 63;
		}

		explicit operator double() const
		{
			auto c1 = upper;
			auto c0 = lower;
			if (upper >> 63) {
				c1 = 0 - c1;
				c1 -= c0 > 0;
				c0 = 0 - c0;
				return -std::fma(c1, ULongMaxValueSD, c0) * std::pow(2.0, -FracBits);
			}
			return std::fma(c1, ULongMaxValueSD, c0) * std::pow(2.0, -FracBits);
		}

		fixed128& operator+=(const fixed128& value)
		{
			return *this = *this + value;
		}

		fixed128& operator+=(fixed64<FracBits> value)
		{
			upper += detail::addcarry(0, lower, value.ubits, &lower);
			return *this;
		}

		fixed128& operator+=(double value)
		{
			FIXED128_CHECK_INPUT("fixed128::operator+=(double)", value, -detail::pow2(127 - FracBits), detail::pow2(127 - FracBits));
			if (value < 0.0) {
				value = std::abs(value) * std::pow(2.0, FracBits); //==bit_cast<double>(bit_cast<uint64_t>(value) & LLONG_MIN)
				double div = value * (1.0 / ULongMaxValueSD);
				double upperTrunc = std::floor(div); //maybe trunc better?
				uint64_t targetUpper = static_cast<uint64_t>(div);
				uint64_t targetLower = static_cast<uint64_t>(std::fma(upperTrunc, -ULongMaxValueSD, value));
				//减法
				lower -= lower < targetLower;
				upper -= targetUpper;
				lower -= targetLower;
			}
			else if (value > 0.0) {
				value *= std::pow(2.0, FracBits);
				double div = value * (1.0 / ULongMaxValueSD);
				double upperTrunc = std::floor(div); //maybe trunc better?
				uint64_t targetUpper = static_cast<uint64_t>(div);
				uint64_t targetLower = static_cast<uint64_t>(std::fma(upperTrunc, -ULongMaxValueSD, value));
				//加法
				lower += targetLower;
				upper = upper + targetUpper + (lower < targetLower);
			}
			return *this;
		}

		fixed128& operator-=(const fixed128& value)
		{
			return *this = *this - value;
		}

		fixed128 operator+(const fixed128& b) const
		{
			uint64_t lo, hi;
			auto carry = detail::addcarry(0, lower, b.lower, &lo);
			detail::addcarry(carry, upper, b.upper, &hi);
			return fixed128{static_cast<int64_t>(hi), static_cast<int64_t>(lo)};
		}

		fixed128 operator-(const fixed128& b) const
		{
			uint64_t lo, hi;
			auto borrow = detail::subborrow(0, lower, b.lower, &lo);
			detail::subborrow(borrow, upper, b.upper, &hi);
			return fixed128{static_cast<int64_t>(hi), static_cast<int64_t>(lo)};
		}
	};

	template<int FracBits>
	std::ostream& operator<<(std::ostream& os, const fixed128<FracBits>& i128)
	{
		return os << std::hex << i128.upper << std::setw(16) << std::setfill('0') << i128.lower;
	}
}
