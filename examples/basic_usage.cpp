// Typical use: positions stored as fixed128, relative positions computed exactly in fixed point,
// then converted to double for floating-point math.
#include <fixed128/fixed128.hpp>

#include <cstdio>

using namespace fx128;

// Number of fractional bits: always named explicitly, there is no default scale.
constexpr int kFracBits = 36;

int main()
{
	// --- scalar ---
	fixed128<kFracBits> a{1000.0};
	const fixed128<kFracBits> b{-0.001};
	std::printf("fixed128   a = %.10f, b = %.10f\n", static_cast<double>(a), static_cast<double>(b));
	for (int i = 0; i < 1000000; ++i) a += b; // exact fixed-point accumulation: 1000 - 1000 * (0.001 in fixed point)
	std::printf("fixed128   a + 1e6 * b = %.10f\n", static_cast<double>(a));

	// --- 4 lanes at once ---
	// Two sets of 4 positions far away from the origin (in meters, ~ 1 million light years), whose difference is
	// small and needs full precision. A double cannot hold such positions to the millimeter (its spacing near 1e22
	// is ~2 million meters), so they are built in fixed point: a far base plus small offsets.
	const fixed128x4<kFracBits> base{9.46e21};
	const fixed128x4<kFracBits> p = base + fixed128x4<kFracBits>{double4{1.0, 2.5, -3.0, 0.125}};
	const fixed128x4<kFracBits> q = base + fixed128x4<kFracBits>{double4{0.5, 4.5, -3.0, -0.125}};
	const double4 delta = static_cast<double4>(p - q); // subtract in fixed point, then convert

	// Expected: { 0.5, -2, 0, 0.25 }
	std::printf("fixed128x4 p - q = { %g, %g, %g, %g }\n", delta.x, delta.y, delta.z, delta.w);

	// Lane access returns a scalar fixed128.
	std::printf("fixed128x4 (p - q)[0] = %g\n", static_cast<double>((p - q)[0]));
	return 0;
}
