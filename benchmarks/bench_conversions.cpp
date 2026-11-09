// Conversion benchmarks (nanobench).
//
// Two kinds of measurement:
//  * throughput: convert an L1-resident array of independent inputs; reported per converted value.
//  * latency:    a dependency chain double -> fixed128 -> double, the shape of the typical use
//                (relative position computed in fixed point, then math in floating point).
//
// Usage: fixed128_bench [substring]   runs only the benchmarks whose name contains the substring.
// Always measure a Release build. Cycle-level numbers in the source comments were derived for Ice Lake;
// use uiCA (https://uica.uops.info) to reproduce them statically.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <fixed128/fixed128.hpp>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace fx128;
using ankerl::nanobench::doNotOptimizeAway;

namespace
{
	constexpr size_t kVectors = 256; // 256 x 32 bytes = 8 KiB per input array, stays in L1
	constexpr size_t kValues = kVectors * 4;

	// Random double whose magnitude in fixed-point units lies in [2^min_log2, 2^max_log2).
	double random_double(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative)
	{
		std::uniform_int_distribution<int> exp_dist(min_log2, max_log2 - 1);
		const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
		const uint64_t bits = sign | (static_cast<uint64_t>(exp_dist(rng) - FIXED128_FRAC_BITS + 1023) << 52) | (rng() & 0xFFFFFFFFFFFFFull);
		double d;
		std::memcpy(&d, &bits, sizeof d);
		return d;
	}

	template <typename T>
	std::vector<T> random_vectors(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative)
	{
		std::vector<T> v(kVectors);
		for (auto& x : v) {
			if constexpr (std::is_same_v<T, double4>)
				x = _mm256_setr_pd(random_double(rng, min_log2, max_log2, allow_negative), random_double(rng, min_log2, max_log2, allow_negative),
					random_double(rng, min_log2, max_log2, allow_negative), random_double(rng, min_log2, max_log2, allow_negative));
			else
				x = _mm256_setr_epi64x(static_cast<int64_t>(rng()), static_cast<int64_t>(rng()), static_cast<int64_t>(rng()), static_cast<int64_t>(rng()));
		}
		return v;
	}

	struct runner
	{
		std::string filter;

		template <typename Op>
		void run(ankerl::nanobench::Bench& bench, const char* name, Op&& op)
		{
			if (filter.empty() || std::string(name).find(filter) != std::string::npos) bench.run(name, op);
		}
	};
}

int main(int argc, char** argv)
{
	runner r{argc > 1 ? argv[1] : ""};
	std::mt19937_64 rng(2022);

	const auto d_int64 = random_vectors<double4>(rng, 0 + FIXED128_FRAC_BITS, 63 + FIXED128_FRAC_BITS, true);
	const auto d_uint64 = random_vectors<double4>(rng, 0 + FIXED128_FRAC_BITS, 64 + FIXED128_FRAC_BITS, false);
	const auto d_fixed128 = random_vectors<double4>(rng, -8, 127, true);
	const auto d_ufixed128 = random_vectors<double4>(rng, -8, 128, false);
	const auto i_bits = random_vectors<__m256i>(rng, 0, 0, false);
	const auto i_bits2 = random_vectors<__m256i>(rng, 0, 0, false);

	std::vector<double4> out_d(kVectors);
	std::vector<__m256i> out_hi(kVectors), out_lo(kVectors);

	ankerl::nanobench::Bench throughput;
	throughput.title("throughput (per value, independent inputs)").unit("value").batch(kValues).warmup(100).minEpochIterations(200);

	r.run(throughput, "double -> int64 (scalar cvttsd2si, baseline)", [&] {
		for (size_t i = 0; i < kVectors; ++i) {
			alignas(32) double in[4];
			alignas(32) int64_t out[4];
			_mm256_store_pd(in, d_int64[i]);
			for (int l = 0; l < 4; ++l) out[l] = static_cast<int64_t>(in[l]);
			out_lo[i] = _mm256_load_si256(reinterpret_cast<const __m256i*>(out));
		}
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "double -> int64 (double_to_int64_fast)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_lo[i] = double_to_int64_fast(d_int64[i]);
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "double -> uint64 (double_to_uint64_fast)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_lo[i] = double_to_uint64_fast(d_uint64[i]);
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "int64 -> double (int64_to_double_fast_precise)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_d[i] = int64_to_double_fast_precise(i_bits[i]);
		doNotOptimizeAway(out_d.data());
	});
	r.run(throughput, "uint64 -> double (uint64_to_double_fast_precise)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_d[i] = uint64_to_double_fast_precise(i_bits[i]);
		doNotOptimizeAway(out_d.data());
	});
	r.run(throughput, "double -> fixed128 (scalar fixed128, baseline)", [&] {
		for (size_t i = 0; i < kVectors; ++i) {
			alignas(32) double in[4];
			_mm256_store_pd(in, d_fixed128[i]);
			const fixed128x4 v{fixed128{in[3]}, fixed128{in[2]}, fixed128{in[1]}, fixed128{in[0]}};
			out_hi[i] = v.upper;
			out_lo[i] = v.lower;
		}
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "double -> fixed128 (double_to_fixed128_full)", [&] {
		for (size_t i = 0; i < kVectors; ++i) double_to_fixed128_full(d_fixed128[i], out_hi[i], out_lo[i]);
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "double -> ufixed128 (double_to_ufixed128_full)", [&] {
		for (size_t i = 0; i < kVectors; ++i) double_to_ufixed128_full(d_ufixed128[i], out_hi[i], out_lo[i]);
		doNotOptimizeAway(out_lo.data());
	});
	r.run(throughput, "fixed128 -> double (scalar fixed128, baseline)", [&] {
		for (size_t i = 0; i < kVectors; ++i) {
			const fixed128x4 v{i_bits[i], i_bits2[i]};
			out_d[i] = _mm256_setr_pd(static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]), static_cast<double>(v[3]));
		}
		doNotOptimizeAway(out_d.data());
	});
	r.run(throughput, "fixed128 -> double (fixed128_to_double)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_d[i] = fixed128_to_double(i_bits[i], i_bits2[i]);
		doNotOptimizeAway(out_d.data());
	});
	r.run(throughput, "ufixed128 -> double (ufixed128_to_double)", [&] {
		for (size_t i = 0; i < kVectors; ++i) out_d[i] = ufixed128_to_double(i_bits[i], i_bits2[i]);
		doNotOptimizeAway(out_d.data());
	});
	r.run(throughput, "fixed128x4 a - b -> double (relative position)", [&] {
		for (size_t i = 0; i + 1 < kVectors; ++i) {
			const fixed128x4 a{i_bits[i], i_bits2[i]}, b{i_bits[i + 1], i_bits2[i + 1]};
			out_d[i] = static_cast<double4>(a - b);
		}
		doNotOptimizeAway(out_d.data());
	});

	ankerl::nanobench::Bench latency;
	latency.title("latency (dependency chain)").unit("round trip").warmup(1000).minEpochIterations(20000);

	double4 chain = d_fixed128[0];
	r.run(latency, "double -> fixed128 -> double round trip", [&] {
		long4 hi, lo;
		double_to_fixed128_full(chain, hi, lo);
		chain = fixed128_to_double(hi, lo);
		doNotOptimizeAway(chain);
	});
	chain = d_int64[0];
	r.run(latency, "double -> int64 -> double round trip", [&] {
		chain = int64_to_double_fast_precise(double_to_int64_fast(chain));
		doNotOptimizeAway(chain);
	});
	return 0;
}
