// Conversion benchmarks (nanobench). Every number answers one question:
//  * throughput:  cost per converted value with independent inputs, at several working-set sizes (L1 .. memory),
//                 for every registered candidate (experimental/candidates) and for baselines;
//  * latency:     a double -> fixed128 -> double dependency chain (the result feeds the next conversion);
//  * scenarios:   realistic uses: positions relative to a camera, pairwise gravity between bodies and the integration
//                 step, each also with double-double (dd4.hpp) and plain double positions for comparison.
//
// Usage: fixed128_bench [--filter TEXT] [--sizes l1,l2,l3,mem] [--json FILE] [--ghz GHZ]
//   --filter  run only benchmarks whose title or name contains TEXT (a bare argument works too)
//   --sizes   working-set tiers of the throughput benchmarks (default l1,l2,l3; mem uses ~400 MiB)
//   --json    also write all results and the environment to FILE (compare runs with tools/bench/Compare-Bench.ps1)
//   --ghz     print cycles per value = ns * GHz (lock the CPU frequency first; on Linux nanobench also reads the
//             hardware cycle counter itself)
// Always measure a Release build, with the CPU frequency fixed and the machine otherwise idle.
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include "dd4.hpp"

#include <candidates/candidates.hpp>
#include <fixed128/fixed128.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <immintrin.h>
#endif

using namespace fx128;
using ankerl::nanobench::doNotOptimizeAway;

// The scale the conversions are benchmarked at; their cost does not depend on it.
constexpr int kFracBits = 36;

#ifndef FIXED128_BENCH_FLAGS
#define FIXED128_BENCH_FLAGS "unknown"
#endif
#ifndef FIXED128_GIT_DESCRIBE
#define FIXED128_GIT_DESCRIBE "unknown"
#endif

namespace
{
	// ---- environment ----

	std::string cpu_name()
	{
#if defined(_M_X64) || defined(__x86_64__)
		unsigned regs[12] = {};
		for (unsigned i = 0; i < 3; ++i) {
#if defined(_MSC_VER)
			int r[4];
			__cpuid(r, static_cast<int>(0x80000002u + i));
			std::memcpy(regs + 4 * i, r, sizeof r);
#else
			__get_cpuid(0x80000002u + i, regs + 4 * i, regs + 4 * i + 1, regs + 4 * i + 2, regs + 4 * i + 3);
#endif
		}
		char name[49] = {};
		std::memcpy(name, regs, 48);
		std::string s(name);
		s.erase(0, s.find_first_not_of(' '));
		return s;
#else
		return "unknown (not x86-64)";
#endif
	}

	std::string compiler_name()
	{
		std::ostringstream s;
#if defined(__clang__)
		s << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#elif defined(_MSC_VER)
		s << "MSVC " << _MSC_FULL_VER;
#elif defined(__GNUC__)
		s << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
		s << "unknown";
#endif
		return s.str();
	}

	std::string json_escape(const std::string& s)
	{
		std::string out;
		for (char c : s) {
			if (c == '"' || c == '\\') out += '\\';
			out += c;
		}
		return out;
	}

	// ---- inputs ----

	// Random double whose magnitude in fixed-point units lies in [2^min_log2, 2^max_log2).
	double random_double(std::mt19937_64& rng, int min_log2, int max_log2, bool allow_negative)
	{
		std::uniform_int_distribution<int> exp_dist(min_log2, max_log2 - 1);
		const uint64_t sign = allow_negative ? (rng() & 1) << 63 : 0;
		const uint64_t bits = sign | (static_cast<uint64_t>(exp_dist(rng) - kFracBits + 1023) << 52) | (rng() & 0xFFFFFFFFFFFFFull);
		double d;
		std::memcpy(&d, &bits, sizeof d);
		return d;
	}

	std::vector<double4> random_double4s(std::mt19937_64& rng, size_t n, int min_log2, int max_log2, bool allow_negative)
	{
		std::vector<double4> v(n);
		for (auto& x : v)
			x = {random_double(rng, min_log2, max_log2, allow_negative), random_double(rng, min_log2, max_log2, allow_negative),
				random_double(rng, min_log2, max_log2, allow_negative), random_double(rng, min_log2, max_log2, allow_negative)};
		return v;
	}

	std::vector<ulong4> random_ulong4s(std::mt19937_64& rng, size_t n)
	{
		std::vector<ulong4> v(n);
		for (auto& x : v) x = {rng(), rng(), rng(), rng()};
		return v;
	}

	// ---- running ----

	struct options
	{
		std::string filter;
		std::vector<std::string> sizes = {"l1", "l2", "l3"};
		std::string json;
		double ghz = 0;
	};

	struct runner
	{
		options opt;
		std::vector<ankerl::nanobench::Bench*> benches;

		bool wanted(const ankerl::nanobench::Bench& bench, const std::string& name) const
		{
			return opt.filter.empty() || name.find(opt.filter) != std::string::npos || bench.title().find(opt.filter) != std::string::npos;
		}

		template <typename Op>
		void run(ankerl::nanobench::Bench& bench, const std::string& name, Op&& op)
		{
			if (!wanted(bench, name)) return;
			bench.run(name, op);
			if (opt.ghz > 0) {
				// median(elapsed) is per iteration of the lambda, i.e. per batch
				const double ns = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9 / bench.batch();
				std::cout << "    = " << ns * opt.ghz << " cycles per " << bench.unit() << " at " << opt.ghz << " GHz\n";
			}
		}
	};

	// Working-set tiers: bytes of input per tier (outputs are about as large).
	struct tier
	{
		const char* name;
		size_t input_bytes;
	};
	constexpr tier kTiers[] = {{"l1", 8u << 10}, {"l2", 256u << 10}, {"l3", 8u << 20}, {"mem", 128u << 20}};

	// ---- throughput of every candidate and baseline at one working-set size ----

	void run_throughput(runner& r, const tier& t, std::vector<ankerl::nanobench::Bench>& storage)
	{
		const size_t n = t.input_bytes / sizeof(double4); // vectors of 4 values
		std::mt19937_64 rng(2022);
		const auto d_signed = random_double4s(rng, n, -8, 127, true);
		const auto d_unsigned = random_double4s(rng, n, -8, 128, false);
		const auto d_int64 = random_double4s(rng, n, 0 + kFracBits, 63 + kFracBits, true);
		const auto hi = random_ulong4s(rng, n), lo = random_ulong4s(rng, n);
		std::vector<double4> out_d(n);
		std::vector<ulong4> out_hi(n), out_lo(n);

		storage.emplace_back();
		ankerl::nanobench::Bench& b = storage.back();
		const std::string size = std::string(t.name) + " (" + std::to_string(t.input_bytes >> 10) + " KiB input)";
		b.title("throughput, " + size + ", " + (FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar") + " backend")
			.unit("value").batch(n * 4).warmup(3).minEpochIterations(t.input_bytes > (1u << 20) ? 1 : 20).relative(false);
		r.benches.push_back(&b);

		candidates::for_each_type<candidates::all>([&](auto candidate) {
			using C = typename decltype(candidate)::type;
			const std::string who = std::string(" [") + C::name + "]";
			if constexpr (candidates::has_double_to_fixed128<C>::value)
				r.run(b, "double -> fixed128" + who, [&] {
					for (size_t i = 0; i < n; ++i) C::template double_to_fixed128<kFracBits>(d_signed[i], out_hi[i], out_lo[i]);
					doNotOptimizeAway(out_lo.data());
				});
			if constexpr (candidates::has_double_to_ufixed128<C>::value)
				r.run(b, "double -> ufixed128" + who, [&] {
					for (size_t i = 0; i < n; ++i) C::template double_to_ufixed128<kFracBits>(d_unsigned[i], out_hi[i], out_lo[i]);
					doNotOptimizeAway(out_lo.data());
				});
			if constexpr (candidates::has_fixed128_to_double<C>::value)
				r.run(b, "fixed128 -> double" + who, [&] {
					for (size_t i = 0; i < n; ++i) out_d[i] = C::template fixed128_to_double<kFracBits>(hi[i], lo[i]);
					doNotOptimizeAway(out_d.data());
				});
			if constexpr (candidates::has_ufixed128_to_double<C>::value)
				r.run(b, "ufixed128 -> double" + who, [&] {
					for (size_t i = 0; i < n; ++i) out_d[i] = C::template ufixed128_to_double<kFracBits>(hi[i], lo[i]);
					doNotOptimizeAway(out_d.data());
				});
			if constexpr (candidates::has_subtract_to_double<C>::value || candidates::has_subtract_to_float<C>::value) {
				// differences up to 2^86 units (about 1e15 m at 36 frac bits: camera-relative), within the candidate's domain
				const int max_log2 = std::min(86, candidates::subtract_max_log2<C>::value - 1);
				std::mt19937_64 prng(2023);
				std::vector<fixed128x4<kFracBits>> pa, pb;
				for (size_t i = 0; i < n; ++i) {
					pb.push_back({hi[i], lo[i]});
					const double4 d{random_double(prng, -8, max_log2, true), random_double(prng, -8, max_log2, true),
						random_double(prng, -8, max_log2, true), random_double(prng, -8, max_log2, true)};
					pa.push_back(pb.back() + fixed128x4<kFracBits>{d});
				}
				if constexpr (candidates::has_subtract_to_double<C>::value)
					r.run(b, "subtract_to_double" + who, [&] {
						for (size_t i = 0; i < n; ++i) out_d[i] = C::template subtract_to_double<kFracBits>(pa[i], pb[i]);
						doNotOptimizeAway(out_d.data());
					});
				if constexpr (candidates::has_subtract_to_float<C>::value) {
					std::vector<float4> out_f(n);
					r.run(b, "subtract_to_float" + who, [&] {
						for (size_t i = 0; i < n; ++i) out_f[i] = C::template subtract_to_float<kFracBits>(pa[i], pb[i]);
						doNotOptimizeAway(out_f.data());
					});
				}
			}
			if constexpr (candidates::has_add_double<C>::value) {
				// steps up to 2^60 units (about 16000 km at 36 frac bits), within the candidate's domain
				const int max_log2 = std::min(60, candidates::add_max_log2<C>::value);
				std::mt19937_64 prng(2024);
				std::vector<fixed128x4<kFracBits>> pa, out_p(n);
				std::vector<double4> steps;
				for (size_t i = 0; i < n; ++i) {
					pa.push_back({hi[i], lo[i]});
					steps.push_back({random_double(prng, -8, max_log2, true), random_double(prng, -8, max_log2, true),
						random_double(prng, -8, max_log2, true), random_double(prng, -8, max_log2, true)});
				}
				r.run(b, "fixed128 + double (add_double)" + who, [&] {
					for (size_t i = 0; i < n; ++i) out_p[i] = C::template add_double<kFracBits>(pa[i], steps[i]);
					doNotOptimizeAway(out_p.data());
				});
			}
		});

#if defined(__SIZEOF_INT128__) && !defined(_WIN32)
		// The compiler's own 128-bit conversions (library calls such as __floattidf / __fixdfti).
		r.run(b, "fixed128 -> double [compiler __int128, baseline]", [&] {
			const double scale = std::ldexp(1.0, -kFracBits);
			for (size_t i = 0; i < n; ++i) {
				auto lane = [&](uint64_t h, uint64_t l) { return static_cast<double>(static_cast<__int128>((static_cast<unsigned __int128>(h) << 64) | l)) * scale; };
				out_d[i] = {lane(hi[i].x, lo[i].x), lane(hi[i].y, lo[i].y), lane(hi[i].z, lo[i].z), lane(hi[i].w, lo[i].w)};
			}
			doNotOptimizeAway(out_d.data());
		});
		r.run(b, "double -> fixed128 [compiler __int128, baseline]", [&] {
			const double scale = std::ldexp(1.0, kFracBits);
			for (size_t i = 0; i < n; ++i) {
				auto lane = [&](double v, uint64_t& h, uint64_t& l) {
					const __int128 x = static_cast<__int128>(v * scale);
					h = static_cast<uint64_t>(static_cast<unsigned __int128>(x) >> 64);
					l = static_cast<uint64_t>(x);
				};
				lane(d_signed[i].x, out_hi[i].x, out_lo[i].x);
				lane(d_signed[i].y, out_hi[i].y, out_lo[i].y);
				lane(d_signed[i].z, out_hi[i].z, out_lo[i].z);
				lane(d_signed[i].w, out_hi[i].w, out_lo[i].w);
			}
			doNotOptimizeAway(out_lo.data());
		});
#endif

		// 64-bit conversions: the library's emulations vs the scalar hardware instructions (and AVX-512 if compiled for it).
		std::vector<long4> out_s(n);
		r.run(b, "double -> int64 [library double_to_int64_fast]", [&] {
			for (size_t i = 0; i < n; ++i) out_s[i] = double_to_int64_fast(d_int64[i]);
			doNotOptimizeAway(out_s.data());
		});
		r.run(b, "double -> int64 [scalar hardware, baseline]", [&] {
			for (size_t i = 0; i < n; ++i) {
				const double4& v = d_int64[i];
				out_s[i] = {static_cast<int64_t>(v.x), static_cast<int64_t>(v.y), static_cast<int64_t>(v.z), static_cast<int64_t>(v.w)};
			}
			doNotOptimizeAway(out_s.data());
		});
		// Inputs prepared outside the timed loop: building a long4 lane by lane right before a vector load would measure
		// a store-forwarding stall instead of the conversion.
		std::vector<long4> s_in(n);
		for (size_t i = 0; i < n; ++i)
			s_in[i] = {static_cast<int64_t>(hi[i].x), static_cast<int64_t>(hi[i].y), static_cast<int64_t>(hi[i].z), static_cast<int64_t>(hi[i].w)};
		r.run(b, "int64 -> double [library int64_to_double_fast_precise]", [&] {
			for (size_t i = 0; i < n; ++i) out_d[i] = int64_to_double_fast_precise(s_in[i]);
			doNotOptimizeAway(out_d.data());
		});
		r.run(b, "int64 -> double [scalar hardware, baseline]", [&] {
			for (size_t i = 0; i < n; ++i) {
				const ulong4& s = hi[i];
				out_d[i] = {static_cast<double>(static_cast<int64_t>(s.x)), static_cast<double>(static_cast<int64_t>(s.y)),
					static_cast<double>(static_cast<int64_t>(s.z)), static_cast<double>(static_cast<int64_t>(s.w))};
			}
			doNotOptimizeAway(out_d.data());
		});
#if defined(__AVX512DQ__) && defined(__AVX512VL__)
		// Upper bound: the native instructions this library emulates, on CPUs with AVX-512DQ/VL.
		r.run(b, "double -> int64 [AVX-512 vcvttpd2qq, upper bound]", [&] {
			for (size_t i = 0; i < n; ++i)
				_mm256_store_si256(reinterpret_cast<__m256i*>(&out_s[i]), _mm256_cvttpd_epi64(_mm256_load_pd(&d_int64[i].x)));
			doNotOptimizeAway(out_s.data());
		});
		r.run(b, "int64 -> double [AVX-512 vcvtqq2pd, upper bound]", [&] {
			for (size_t i = 0; i < n; ++i)
				_mm256_store_pd(&out_d[i].x, _mm256_cvtepi64_pd(_mm256_load_si256(reinterpret_cast<const __m256i*>(&hi[i]))));
			doNotOptimizeAway(out_d.data());
		});
#endif
	}

	// ---- latency: dependency chains ----

	void run_latency(runner& r, std::vector<ankerl::nanobench::Bench>& storage)
	{
		storage.emplace_back();
		ankerl::nanobench::Bench& b = storage.back();
		b.title(std::string("latency (dependency chain), ") + (FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar") + " backend")
			.unit("round trip").warmup(1000).minEpochIterations(20000).relative(false);
		r.benches.push_back(&b);

		candidates::for_each_type<candidates::all>([&](auto candidate) {
			using C = typename decltype(candidate)::type;
			if constexpr (candidates::has_double_to_fixed128<C>::value && candidates::has_fixed128_to_double<C>::value) {
				double4 chain{1.25e10, -3.5e5, 7.0, -1e-3};
				r.run(b, std::string("double -> fixed128 -> double [") + C::name + "]", [&] {
					ulong4 h, l;
					C::template double_to_fixed128<kFracBits>(chain, h, l);
					chain = C::template fixed128_to_double<kFracBits>(h, l);
					doNotOptimizeAway(chain);
				});
			}
		});
		double4 chain{1.25e10, -3.5e5, 7.0, -1e-3};
		r.run(b, "double -> int64 -> double [library]", [&] {
			chain = int64_to_double_fast_precise(double_to_int64_fast(chain));
			doNotOptimizeAway(chain);
		});
	}

	// ---- scenarios ----

	// Positions of bodies around one point far from the origin (~1 million light years): in fixed point, in
	// double-double (the usual alternative, see dd4.hpp) and in double (precision lost that far away).
	struct bodies
	{
		std::vector<fixed128x4<kFracBits>> x, y, z; // 4 bodies per element
		std::vector<dd4::value> ddx, ddy, ddz;
		std::vector<double4> dx, dy, dz;

		explicit bodies(size_t count)
		{
			std::mt19937_64 rng(7);
			std::uniform_real_distribution<double> offset(-1e9, 1e9);
			const fixed128x4<kFracBits> far{9.46e21};
			auto make = [&](std::vector<fixed128x4<kFracBits>>& f, std::vector<dd4::value>& dd, std::vector<double4>& d) {
				const double4 o{offset(rng), offset(rng), offset(rng), offset(rng)};
				f.push_back(far + fixed128x4<kFracBits>{o});
				dd.push_back(to_dd(f.back()));
				d.push_back(dd.back().hi);
			};
			for (size_t i = 0; i < count / 4; ++i) {
				make(x, ddx, dx);
				make(y, ddy, dy);
				make(z, ddz, dz);
			}
		}

		// The double-double nearest to a fixed-point position (hi is exact in fixed point, so the low part is too).
		static dd4::value to_dd(const fixed128x4<kFracBits>& p)
		{
			const double4 hi = static_cast<double4>(p);
			return {hi, static_cast<double4>(p - fixed128x4<kFracBits>{hi})};
		}
	};

	// Lane i % 4 of element i / 4, in all 4 lanes.
	double4 broadcast(const std::vector<double4>& v, size_t i)
	{
		const double d = (&v[i / 4].x)[i % 4];
		return {d, d, d, d};
	}

	void run_scenarios(runner& r, std::vector<ankerl::nanobench::Bench>& storage)
	{
		using fx128::detail::to_double4;
		const std::string backend = FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar";

		// Camera-relative positions: every frame, each body's position relative to the camera, as double or float.
		{
			storage.emplace_back();
			ankerl::nanobench::Bench& b = storage.back();
			b.title("scenario: camera-relative positions, " + backend + " backend").unit("body").warmup(3).relative(false);
			r.benches.push_back(&b);
			const size_t count = 1 << 14;
			const bodies s(count);
			const fixed128x4<kFracBits> cam{9.46e21 + 1.0};
			const dd4::value cam_dd = bodies::to_dd(cam);
			const double cam_d = cam_dd.hi.x;
			std::vector<double4> rx(count / 4), ry(count / 4), rz(count / 4);
			std::vector<float4> fx(count / 4), fy(count / 4), fz(count / 4);
			b.batch(count).minEpochIterations(20);
			r.run(b, "camera-relative positions [fixed128, subtract_to_double]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					rx[i] = subtract_to_double(s.x[i], cam);
					ry[i] = subtract_to_double(s.y[i], cam);
					rz[i] = subtract_to_double(s.z[i], cam);
				}
				doNotOptimizeAway(rx.data());
			});
			r.run(b, "camera-relative positions [fixed128, subtract_to_float]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					fx[i] = subtract_to_float(s.x[i], cam);
					fy[i] = subtract_to_float(s.y[i], cam);
					fz[i] = subtract_to_float(s.z[i], cam);
				}
				doNotOptimizeAway(fx.data());
			});
			r.run(b, "camera-relative positions [double-double, accurate]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					rx[i] = to_double4(dd4::subtract_to_double(s.ddx[i], cam_dd));
					ry[i] = to_double4(dd4::subtract_to_double(s.ddy[i], cam_dd));
					rz[i] = to_double4(dd4::subtract_to_double(s.ddz[i], cam_dd));
				}
				doNotOptimizeAway(rx.data());
			});
			r.run(b, "camera-relative positions [double-double, sloppy]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					rx[i] = to_double4(dd4::subtract_to_double_sloppy(s.ddx[i], cam_dd));
					ry[i] = to_double4(dd4::subtract_to_double_sloppy(s.ddy[i], cam_dd));
					rz[i] = to_double4(dd4::subtract_to_double_sloppy(s.ddz[i], cam_dd));
				}
				doNotOptimizeAway(rx.data());
			});
			r.run(b, "camera-relative positions [double, baseline]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					rx[i] = {s.dx[i].x - cam_d, s.dx[i].y - cam_d, s.dx[i].z - cam_d, s.dx[i].w - cam_d};
					ry[i] = {s.dy[i].x - cam_d, s.dy[i].y - cam_d, s.dy[i].z - cam_d, s.dy[i].w - cam_d};
					rz[i] = {s.dz[i].x - cam_d, s.dz[i].y - cam_d, s.dz[i].z - cam_d, s.dz[i].w - cam_d};
				}
				doNotOptimizeAway(rx.data());
			});
		}

		// Pairwise gravity: for every pair, the relative position (exact in fixed point), then r / |r|^3 in double.
		{
			storage.emplace_back();
			ankerl::nanobench::Bench& b = storage.back();
			b.title("scenario: pairwise gravity, " + backend + " backend").unit("pair").warmup(3).relative(false);
			r.benches.push_back(&b);
			const size_t count = 256;
			const bodies s(count);
			std::vector<double> ax(count), ay(count), az(count);
			b.batch(count * count).minEpochIterations(3);
			auto accumulate = [&](size_t i, const double4& rx, const double4& ry, const double4& rz) {
				const double4 r2{rx.x * rx.x + ry.x * ry.x + rz.x * rz.x + 1.0, rx.y * rx.y + ry.y * ry.y + rz.y * rz.y + 1.0,
					rx.z * rx.z + ry.z * ry.z + rz.z * rz.z + 1.0, rx.w * rx.w + ry.w * ry.w + rz.w * rz.w + 1.0};
				const double f[4] = {1 / (r2.x * std::sqrt(r2.x)), 1 / (r2.y * std::sqrt(r2.y)), 1 / (r2.z * std::sqrt(r2.z)), 1 / (r2.w * std::sqrt(r2.w))};
				ax[i] += rx.x * f[0] + rx.y * f[1] + rx.z * f[2] + rx.w * f[3];
				ay[i] += ry.x * f[0] + ry.y * f[1] + ry.z * f[2] + ry.w * f[3];
				az[i] += rz.x * f[0] + rz.y * f[1] + rz.z * f[2] + rz.w * f[3];
			};
			r.run(b, "pairwise gravity [fixed128, subtract_to_double]", [&] {
				for (size_t i = 0; i < count; ++i) {
					const fixed128x4<kFracBits> px{s.x[i / 4][static_cast<int>(i % 4)]}, py{s.y[i / 4][static_cast<int>(i % 4)]},
						pz{s.z[i / 4][static_cast<int>(i % 4)]};
					for (size_t j = 0; j < count / 4; ++j)
						accumulate(i, subtract_to_double(s.x[j], px), subtract_to_double(s.y[j], py), subtract_to_double(s.z[j], pz));
				}
				doNotOptimizeAway(ax.data());
			});
			r.run(b, "pairwise gravity [double-double, accurate]", [&] {
				auto lane = [](const std::vector<dd4::value>& v, size_t i) {
					const double h = (&v[i / 4].hi.x)[i % 4], l = (&v[i / 4].lo.x)[i % 4];
					return dd4::value{{h, h, h, h}, {l, l, l, l}};
				};
				for (size_t i = 0; i < count; ++i) {
					const dd4::value px = lane(s.ddx, i), py = lane(s.ddy, i), pz = lane(s.ddz, i);
					for (size_t j = 0; j < count / 4; ++j)
						accumulate(i, to_double4(dd4::subtract_to_double(s.ddx[j], px)), to_double4(dd4::subtract_to_double(s.ddy[j], py)),
							to_double4(dd4::subtract_to_double(s.ddz[j], pz)));
				}
				doNotOptimizeAway(ax.data());
			});
			r.run(b, "pairwise gravity [double, baseline]", [&] {
				for (size_t i = 0; i < count; ++i) {
					const double4 px = broadcast(s.dx, i), py = broadcast(s.dy, i), pz = broadcast(s.dz, i);
					for (size_t j = 0; j < count / 4; ++j) {
						const double4 &x = s.dx[j], &y = s.dy[j], &z = s.dz[j];
						accumulate(i, {x.x - px.x, x.y - px.x, x.z - px.x, x.w - px.x}, {y.x - py.x, y.y - py.x, y.z - py.x, y.w - py.x},
							{z.x - pz.x, z.y - pz.x, z.z - pz.x, z.w - pz.x});
					}
				}
				doNotOptimizeAway(ax.data());
			});
		}

		// Integration step: position += velocity * dt for every body, the update every integrator does.
		{
			storage.emplace_back();
			ankerl::nanobench::Bench& b = storage.back();
			b.title("scenario: integration step (position += velocity * dt), " + backend + " backend")
				.unit("body").warmup(3).relative(false);
			r.benches.push_back(&b);
			const size_t count = 1 << 14;
			bodies s(count);
			std::vector<double4> vx(count / 4), vy(count / 4), vz(count / 4);
			std::vector<double4> cx(count / 4, double4{0, 0, 0, 0}), cy(cx), cz(cx); // Kahan compensation
			std::mt19937_64 rng(11);
			std::uniform_real_distribution<double> speed(-3e4, 3e4); // m/s, planetary speeds
			for (auto* v : {&vx, &vy, &vz})
				for (double4& e : *v) e = {speed(rng), speed(rng), speed(rng), speed(rng)};
			const double dt = 60.0;
			auto step = [&](const double4& v) { return double4{v.x * dt, v.y * dt, v.z * dt, v.w * dt}; };
			b.batch(count).minEpochIterations(20);
			r.run(b, "integration step [fixed128, operator+=(double4)]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					s.x[i] += step(vx[i]);
					s.y[i] += step(vy[i]);
					s.z[i] += step(vz[i]);
				}
				doNotOptimizeAway(s.x.data());
			});
			r.run(b, "integration step [double-double]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					dd4::add(s.ddx[i], step(vx[i]));
					dd4::add(s.ddy[i], step(vy[i]));
					dd4::add(s.ddz[i], step(vz[i]));
				}
				doNotOptimizeAway(s.ddx.data());
			});
			r.run(b, "integration step [double + Kahan compensation]", [&] {
				auto kahan = [&](double4& x, double4& c, const double4& v) {
					using namespace fx128::detail;
					const vd y = add_d(load(step(v)), load(c)), t = add_d(load(x), y);
					c = to_double4(sub_d(y, sub_d(t, load(x))));
					x = to_double4(t);
				};
				for (size_t i = 0; i < count / 4; ++i) {
					kahan(s.dx[i], cx[i], vx[i]);
					kahan(s.dy[i], cy[i], vy[i]);
					kahan(s.dz[i], cz[i], vz[i]);
				}
				doNotOptimizeAway(s.dx.data());
			});
			r.run(b, "integration step [double, baseline]", [&] {
				for (size_t i = 0; i < count / 4; ++i) {
					const double4 sx = step(vx[i]), sy = step(vy[i]), sz = step(vz[i]);
					s.dx[i] = {s.dx[i].x + sx.x, s.dx[i].y + sx.y, s.dx[i].z + sx.z, s.dx[i].w + sx.w};
					s.dy[i] = {s.dy[i].x + sy.x, s.dy[i].y + sy.y, s.dy[i].z + sy.z, s.dy[i].w + sy.w};
					s.dz[i] = {s.dz[i].x + sz.x, s.dz[i].y + sz.y, s.dz[i].z + sz.z, s.dz[i].w + sz.w};
				}
				doNotOptimizeAway(s.dx.data());
			});
		}
	}

	options parse(int argc, char** argv)
	{
		options o;
		for (int i = 1; i < argc; ++i) {
			const std::string a = argv[i];
			auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
			if (a == "--filter") o.filter = next();
			else if (a == "--json") o.json = next();
			else if (a == "--ghz") o.ghz = std::atof(next().c_str());
			else if (a == "--sizes") {
				o.sizes.clear();
				std::stringstream ss(next());
				for (std::string s; std::getline(ss, s, ',');) o.sizes.push_back(s);
			}
			else o.filter = a;
		}
		return o;
	}
}

int main(int argc, char** argv)
{
	runner r{parse(argc, argv), {}};
	const std::string cpu = cpu_name(), compiler = compiler_name();
	std::cout << "CPU: " << cpu << "\nCompiler: " << compiler << "\nFlags: " << FIXED128_BENCH_FLAGS << "\nCommit: " << FIXED128_GIT_DESCRIBE
			  << "\nBackend: " << (FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar") << ", frac bits " << kFracBits << "\n";

	std::vector<ankerl::nanobench::Bench> storage;
	storage.reserve(16); // the runner keeps pointers into it
	for (const tier& t : kTiers)
		for (const std::string& s : r.opt.sizes)
			if (s == t.name) run_throughput(r, t, storage);
	run_latency(r, storage);
	run_scenarios(r, storage);

	if (!r.opt.json.empty()) {
		std::ofstream out(r.opt.json);
		out << "{\n  \"environment\": {\"cpu\": \"" << json_escape(cpu) << "\", \"compiler\": \"" << json_escape(compiler)
			<< "\", \"flags\": \"" << json_escape(FIXED128_BENCH_FLAGS) << "\", \"commit\": \"" << json_escape(FIXED128_GIT_DESCRIBE)
			<< "\", \"backend\": \"" << (FIXED128_BACKEND_AVX2 ? "AVX2" : "scalar") << "\"},\n  \"benches\": [\n";
		bool first = true;
		for (const ankerl::nanobench::Bench* b : r.benches) {
			if (b->results().empty()) continue;
			if (!first) out << ",\n";
			first = false;
			ankerl::nanobench::render(ankerl::nanobench::templates::json(), *b, out);
		}
		out << "\n  ]\n}\n";
		std::cout << "wrote " << r.opt.json << "\n";
	}
	return 0;
}
