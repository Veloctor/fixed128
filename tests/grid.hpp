#pragma once
// Structured exhaustive grids: instead of relying on random sampling alone, enumerate the structure of the inputs.
//  double -> fixed128: every exponent of the domain x a fixed set of mantissa patterns x both neighbors x sign.
//  fixed128 -> double: every position of the most significant bit x patterns of the bits that decide rounding.
#include "reference.hpp"

#include <cstdint>
#include <vector>

namespace grid
{
	// Mantissa patterns (52 explicit bits) that stress shifting, carries and rounding:
	// zero, all ones, every single bit, alternating bits, runs of ones from the top and from the bottom.
	inline std::vector<uint64_t> mantissa_patterns()
	{
		constexpr uint64_t mask = 0xFFFFFFFFFFFFFull;
		std::vector<uint64_t> m = {0, mask, 0x5555555555555ull, 0xAAAAAAAAAAAAAull};
		for (int b = 0; b < 52; ++b) m.push_back(1ull << b);
		for (int k = 1; k < 52; k += 3) {
			m.push_back(mask & ~(mask >> k)); // top k bits
			m.push_back(mask >> (52 - k));    // bottom k bits
		}
		return m;
	}

	// Doubles whose scaled magnitude |v| * 2^frac_bits lies in [2^min_log2, 2^max_log2), with every exponent,
	// every mantissa pattern and the next double below and above each of them.
	inline std::vector<double> doubles(int min_log2, int max_log2, int frac_bits, bool with_negative)
	{
		const std::vector<uint64_t> patterns = mantissa_patterns();
		std::vector<double> v;
		for (int e = min_log2; e < max_log2; ++e) {
			const uint64_t exp_bits = static_cast<uint64_t>(e - frac_bits + 1023) << 52;
			for (uint64_t m : patterns) {
				const uint64_t bits = exp_bits | m;
				for (uint64_t b : {bits - 1, bits, bits + 1}) {
					const double d = ref::double_of(b);
					const int log2 = static_cast<int>((b >> 52) & 0x7FF) - 1023 + frac_bits;
					if (log2 < min_log2 || log2 >= max_log2) continue; // a neighbor left the range
					v.push_back(d);
					if (with_negative) v.push_back(-d);
				}
			}
		}
		if (with_negative) v.push_back(-ref::pow2(max_log2 - frac_bits)); // the most negative value
		return v;
	}

	// 128-bit values for every position p of the most significant bit, with the bits below shaped to hit every
	// rounding case of a 53-bit result: exact, below / at / above half, ties to even and odd, all ones, sticky only.
	inline std::vector<ref::u128> fixed128_values(bool with_negative)
	{
		std::vector<ref::u128> v = {{0, 0}};
		for (int p = 0; p < 128; ++p) {
			const ref::u128 top = ref::shl({0, 1}, p);
			const ref::u128 below = ref::sub(top, {0, 1}); // all ones below the top bit
			const int r = p - 53;                          // the rounding bit (first bit not kept), if p >= 53
			std::vector<ref::u128> cases = {top, ref::add(top, below)};
			if (r >= 0) {
				const ref::u128 half = ref::shl({0, 1}, r);
				const ref::u128 lsb = ref::shl({0, 1}, r + 1); // last kept bit
				cases.push_back(ref::add(top, half));                         // tie, even -> stays
				cases.push_back(ref::add(ref::add(top, half), lsb));          // tie, odd -> rounds up
				if (r > 0) {
					cases.push_back(ref::add(ref::add(top, half), {0, 1}));   // just above half
					cases.push_back(ref::add(top, ref::sub(half, {0, 1})));   // just below half
					cases.push_back(ref::add(top, {0, 1}));                   // sticky bit only
				}
			}
			else if (p > 0) {
				cases.push_back(ref::add(top, {0, 1}));
			}
			for (const ref::u128& c : cases) {
				v.push_back(c);
				if (with_negative) v.push_back(ref::neg(c));
			}
		}
		return v;
	}
}
