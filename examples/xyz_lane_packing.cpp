// Shuffling helpers for packing xyz vectors into fixed128x4 lanes.
//
// Layout assumed here: three fixed128x4 registers hold the x, y and z components of up to 4 vectors (SoA).
// When a 4th vector is stored "spare" in lane 3 of each component register, these helpers move it
// into / out of a single fixed128x4 holding (x, y, z, unused) in lanes 0..2.
// They used to live in the library header; they are application-specific, so they are kept here as an example.
#include <fixed128/fixed128.hpp>

#include <cstdio>

using namespace fx128;

inline fixed128x4 gather_3xfix128x4_last_elem_into1(const fixed128x4& in1, const fixed128x4& in2, const fixed128x4& in3)
{
	fixed128x4 out{};
	//operations:
	//out.lower[0] = in1.lower[3];
	//out.lower[1] = in2.lower[3];
	//out.lower[2] = in3.lower[3];
	//out.upper[0] = in1.upper[3];
	//out.upper[1] = in2.upper[3];
	//out.upper[2] = in3.upper[3];
	auto c = _mm256_permute4x64_epi64(in1.lower, 0b11);
	auto d = _mm256_permute4x64_epi64(in2.lower, 0b1100);
	c = _mm256_blend_epi32(c, d, 0b1100);
	auto lo = _mm256_srli_si256(in3.lower, 8);
	out.lower = _mm256_blend_epi32(c, lo, 0b11110000);
	auto a = _mm256_permute4x64_epi64(in1.upper, 0b11);
	auto b = _mm256_permute4x64_epi64(in2.upper, 0b1100);
	a = _mm256_blend_epi32(a, b, 0b1100);
	auto hi = _mm256_srli_si256(in3.upper, 8);
	out.upper = _mm256_blend_epi32(a, hi, 0b11110000);
	return out;
}

inline void distribute_in4xyz_to_3xfix128x4_last_elem(fixed128x4& inout1, fixed128x4& inout2, fixed128x4& inout3, const fixed128x4& in4)
{
	//operations:
	//inout1.lower[3] = in4.lower[0];
	//inout2.lower[3] = in4.lower[1];
	//inout3.lower[3] = in4.lower[2];
	//inout1.upper[3] = in4.upper[0];
	//inout2.upper[3] = in4.upper[1];
	//inout3.upper[3] = in4.upper[2];
	auto a = _mm256_permute4x64_epi64(in4.lower, 0);
	inout1.lower = _mm256_blend_epi32(inout1.lower, a, 0b11000000);
	auto b = _mm256_permute4x64_epi64(in4.lower, 0b01000000);
	inout2.lower = _mm256_blend_epi32(inout2.lower, b, 0b11000000);
	auto c = _mm256_slli_si256(in4.lower, 8);
	inout3.lower = _mm256_blend_epi32(inout3.lower, c, 0b11000000);
	auto d = _mm256_permute4x64_epi64(in4.upper, 0);
	inout1.upper = _mm256_blend_epi32(inout1.upper, d, 0b11000000);
	auto e = _mm256_permute4x64_epi64(in4.upper, 0b01000000);
	inout2.upper = _mm256_blend_epi32(inout2.upper, e, 0b11000000);
	auto f = _mm256_slli_si256(in4.upper, 8);
	inout3.upper = _mm256_blend_epi32(inout3.upper, f, 0b11000000);
}

int main()
{
	// x, y, z component registers; lane 3 holds the spare vector (10, 20, 30).
	fixed128x4 xs{fixed128{0, 10}, fixed128{0, 1}, fixed128{0, 1}, fixed128{0, 1}};
	fixed128x4 ys{fixed128{0, 20}, fixed128{0, 2}, fixed128{0, 2}, fixed128{0, 2}};
	fixed128x4 zs{fixed128{0, 30}, fixed128{0, 3}, fixed128{0, 3}, fixed128{0, 3}};

	const fixed128x4 xyz = gather_3xfix128x4_last_elem_into1(xs, ys, zs);
	std::printf("gathered: x=%llu y=%llu z=%llu\n", (unsigned long long)xyz[0].lower, (unsigned long long)xyz[1].lower,
		(unsigned long long)xyz[2].lower);

	const fixed128x4 moved{fixed128{0, 0}, fixed128{0, 33}, fixed128{0, 22}, fixed128{0, 11}};
	distribute_in4xyz_to_3xfix128x4_last_elem(xs, ys, zs, moved);
	std::printf("distributed: x=%llu y=%llu z=%llu\n", (unsigned long long)xs[3].lower, (unsigned long long)ys[3].lower,
		(unsigned long long)zs[3].lower);
	return 0;
}
