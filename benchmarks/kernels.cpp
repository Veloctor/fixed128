// The conversion kernels and fused operations as standalone, non-inlined functions at a fixed scale, so that their compiled assembly can be
// read and analyzed statically (uiCA, llvm-mca). Build the target fixed128_kernels; see tools/bench/README.md.
#include <fixed128/fixed128.hpp>

using namespace fx128;

constexpr int kFracBits = 36;

#if defined(_MSC_VER)
#define KERNEL extern "C" __declspec(noinline)
#else
#define KERNEL extern "C" __attribute__((noinline))
#endif

KERNEL void kernel_double_to_fixed128(const double4* in, ulong4* upper, ulong4* lower)
{
	double_to_fixed128_full<kFracBits>(*in, *upper, *lower);
}

KERNEL void kernel_double_to_ufixed128(const double4* in, ulong4* upper, ulong4* lower)
{
	double_to_ufixed128_full<kFracBits>(*in, *upper, *lower);
}

KERNEL void kernel_fixed128_to_double(const ulong4* upper, const ulong4* lower, double4* out)
{
	*out = fixed128_to_double<kFracBits>(*upper, *lower);
}

KERNEL void kernel_ufixed128_to_double(const ulong4* upper, const ulong4* lower, double4* out)
{
	*out = ufixed128_to_double<kFracBits>(*upper, *lower);
}

KERNEL void kernel_double_to_int64(const double4* in, long4* out)
{
	*out = double_to_int64_fast(*in);
}

KERNEL void kernel_int64_to_double(const long4* in, double4* out)
{
	*out = int64_to_double_fast_precise(*in);
}

// A loop over an array, the shape uiCA analyzes best (it reports the steady-state throughput of the loop body).
KERNEL void loop_fixed128_to_double(const ulong4* upper, const ulong4* lower, double4* out, long n)
{
	for (long i = 0; i < n; ++i) out[i] = fixed128_to_double<kFracBits>(upper[i], lower[i]);
}

KERNEL void loop_double_to_fixed128(const double4* in, ulong4* upper, ulong4* lower, long n)
{
	for (long i = 0; i < n; ++i) double_to_fixed128_full<kFracBits>(in[i], upper[i], lower[i]);
}

// Fused operations: relative positions against a fixed point (the camera), and position += displacement.
KERNEL void loop_subtract_to_double(const fixed128x4<kFracBits>* a, const fixed128x4<kFracBits>* camera, double4* out, long n)
{
	const fixed128x4<kFracBits> c = *camera;
	for (long i = 0; i < n; ++i) out[i] = subtract_to_double(a[i], c);
}

KERNEL void loop_subtract_to_float(const fixed128x4<kFracBits>* a, const fixed128x4<kFracBits>* camera, float4* out, long n)
{
	const fixed128x4<kFracBits> c = *camera;
	for (long i = 0; i < n; ++i) out[i] = subtract_to_float(a[i], c);
}

KERNEL void loop_add_double(fixed128x4<kFracBits>* position, const double4* displacement, long n)
{
	for (long i = 0; i < n; ++i) position[i] += displacement[i];
}
