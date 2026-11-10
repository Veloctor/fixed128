// Orbit drift experiment: does the representation of positions (double, double with Kahan summation, fixed128) let
// rounding errors accumulate over long integrations? See docs/test-and-benchmark-plan.md, part five.
//
// System: Sun, Jupiter and the Galilean moons at J2000 (initial states from himīśā's Ephemeris,
// https://github.com/himisawww/Ephemeris, initials/SolarSystem/SolarSystem_Initial.txt; barycentric, ecliptic frame).
// Every variant runs the same symplectic integrator with the same step; only the arithmetic of the positions differs.
// The reference runs the same integrator in double-double, so "variant - reference" contains rounding errors only.
//
// Usage: fixed128_orbit_drift [--years Y] [--integrator fr4|leapfrog] [--steps-per-orbit N] [--offset METERS]
//                             [--runs R] [--check]
//   --steps-per-orbit  steps per orbit of Io (1.77 days); default 256 (fr4) or 2048 (leapfrog)
//   --offset           add this to every coordinate (e.g. 1e21 m ~ 100 000 light years): the scale fixed128 is made for
//   --runs             repeat with tiny perturbations of the initial state; one run is one sample of the rounding noise
//   --check            exit with an error unless fixed128 positions are at least as accurate as double positions
#include "dd.hpp"

#include <fixed128/fixed128.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

constexpr int kFracBits = 36;
using fixed_t = fx128::fixed128x4<kFracBits>;

namespace
{
	struct body_data
	{
		const char* name;
		double gm;       // m^3 / s^2
		double x[3], v[3]; // m, m / s
	};

	// J2000 TDB, from Ephemeris' SolarSystem_Initial.txt.
	const body_data kBodies[] = {
		{"Sun", 1.327124400412795E20, {-1.067706806453718E9, -4.182752704451427E8, 3.086181725430511E7}, {9.31257190034418E0, -1.282475571069859E1, -1.6335071235548E-1}},
		{"Jupiter", 1.266865319003704E17, {5.974999179613324E11, 4.391864047925301E11, -1.519599989690655E10}, {-7.900547721929482E3, 1.114339277880856E4, 1.307023298008232E2}},
		{"Io", 5.959915466180539E12, {5.978996322270731E11, 4.393156715259319E11, -1.51853366555969E10}, {-1.329763037812823E4, 2.767781233330289E4, 6.361338988680136E2}},
		{"Europa", 3.202712099607295E12, {5.969386734596958E11, 4.388303917521267E11, -1.521395398275855E10}, {-4.382535707159293E2, -5.451080210684135E2, -8.65866684176086E1}},
		{"Ganymede", 9.887832752719638E12, {5.966785728577158E11, 4.385010159694557E11, -1.523055768677486E10}, {-9.12910731272726E2, 2.805854032488588E3, -9.96438652533401E1}},
		{"Callisto", 7.179283402579837E12, {5.978249976930955E11, 4.41038664810457E11, -1.513124512240641E10}, {-1.597352006983896E4, 1.262412164454912E4, 7.226661102912226E1}},
	};
	constexpr int kN = static_cast<int>(sizeof kBodies / sizeof kBodies[0]);
	constexpr int kJupiter = 1;
	constexpr double kIoPeriod = 1.769137786 * 86400; // s
	constexpr double kYear = 365.25 * 86400;

	using vec_dd = std::array<dd, 3>;

	// State of all bodies in double-double, positions without the offset: what every variant is compared in.
	struct snapshot
	{
		std::array<vec_dd, kN> x, v;
	};

	// ---- integrators: sequences of drift (x += v * c * dt) and kick (v += a(x) * c * dt) ----

	struct substep
	{
		bool drift;
		double c;
	};

	std::vector<substep> integrator(const std::string& name)
	{
		if (name == "leapfrog") return {{true, 0.5}, {false, 1.0}, {true, 0.5}};
		// Forest-Ruth 4th order, position first (the integrator of the game).
		const double theta = 1.0 / (2.0 - std::cbrt(2.0));
		return {{true, theta / 2}, {false, theta}, {true, (1 - theta) / 2}, {false, 1 - 2 * theta},
			{true, (1 - theta) / 2}, {false, theta}, {true, theta / 2}};
	}

	// ---- variants ----

	// Reference: everything in double-double, positions without offset.
	struct reference_variant
	{
		static constexpr const char* name = "reference (double-double)";
		std::array<vec_dd, kN> x, v;
		std::array<dd, kN> gm;

		void init(const body_data* b, const double (*dx)[3], double /*offset*/)
		{
			for (int i = 0; i < kN; ++i) {
				gm[i] = b[i].gm;
				for (int k = 0; k < 3; ++k) x[i][k] = dd(b[i].x[k]) + dd(dx[i][k]), v[i][k] = b[i].v[k];
			}
		}

		void drift(double h)
		{
			for (int i = 0; i < kN; ++i)
				for (int k = 0; k < 3; ++k) x[i][k] += v[i][k] * dd(h);
		}

		void kick(double h)
		{
			std::array<vec_dd, kN> a{};
			for (int i = 0; i < kN; ++i)
				for (int j = i + 1; j < kN; ++j) {
					vec_dd r;
					for (int k = 0; k < 3; ++k) r[k] = x[j][k] - x[i][k];
					const dd r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
					const dd inv_r3 = dd(1) / (r2 * sqrt(r2));
					for (int k = 0; k < 3; ++k) {
						a[i][k] += gm[j] * inv_r3 * r[k];
						a[j][k] -= gm[i] * inv_r3 * r[k];
					}
				}
			for (int i = 0; i < kN; ++i)
				for (int k = 0; k < 3; ++k) v[i][k] += a[i][k] * dd(h);
		}

		snapshot snap() const { return {x, v}; }
	};

	// Accelerations from relative positions given in double (shared by the double-based variants).
	template <class RelPos>
	void kick_double(std::array<std::array<double, 3>, kN>& v, const double* gm, double h, RelPos&& rel)
	{
		double a[kN][3] = {};
		for (int i = 0; i < kN; ++i)
			for (int j = i + 1; j < kN; ++j) {
				double r[3];
				rel(i, j, r); // r = x_j - x_i
				const double r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
				const double inv_r3 = 1.0 / (r2 * std::sqrt(r2));
				for (int k = 0; k < 3; ++k) {
					a[i][k] += gm[j] * inv_r3 * r[k];
					a[j][k] -= gm[i] * inv_r3 * r[k];
				}
			}
		for (int i = 0; i < kN; ++i)
			for (int k = 0; k < 3; ++k) v[i][k] += a[i][k] * h;
	}

	// Plain double positions (with the offset added), optionally with Kahan-compensated position updates.
	template <bool Kahan>
	struct double_variant
	{
		static constexpr const char* name = Kahan ? "double + Kahan summation" : "double";
		std::array<std::array<double, 3>, kN> x, c, v;
		double gm[kN];
		double offset = 0;

		void init(const body_data* b, const double (*dx)[3], double off)
		{
			offset = off;
			for (int i = 0; i < kN; ++i) {
				gm[i] = b[i].gm;
				for (int k = 0; k < 3; ++k) {
					const dd exact = dd(b[i].x[k]) + dd(dx[i][k]) + dd(off);
					x[i][k] = exact.hi;
					c[i][k] = Kahan ? exact.lo : 0.0; // Kahan keeps the part the double cannot hold
					v[i][k] = b[i].v[k];
				}
			}
		}

		void drift(double h)
		{
			for (int i = 0; i < kN; ++i)
				for (int k = 0; k < 3; ++k) {
					if (Kahan) {
						const double y = v[i][k] * h + c[i][k];
						const double t = x[i][k] + y;
						c[i][k] = y - (t - x[i][k]);
						x[i][k] = t;
					}
					else {
						x[i][k] += v[i][k] * h;
					}
				}
		}

		void kick(double h)
		{
			kick_double(v, gm, h, [&](int i, int j, double* r) {
				for (int k = 0; k < 3; ++k) r[k] = (x[j][k] - x[i][k]) + (c[j][k] - c[i][k]);
			});
		}

		snapshot snap() const
		{
			snapshot s;
			for (int i = 0; i < kN; ++i)
				for (int k = 0; k < 3; ++k) {
					s.x[i][k] = dd(x[i][k]) + dd(c[i][k]) - dd(offset);
					s.v[i][k] = v[i][k];
				}
			return s;
		}
	};

	// fixed128 positions (one fixed128x4 per body: x, y, z, unused), double velocities. Displacements are converted to
	// fixed point with the library's rounding; relative positions are subtracted exactly and then converted to double.
	struct fixed_variant
	{
		static constexpr const char* name = FIXED128_ROUND_HALF_AWAY_FROM_ZERO ? "fixed128 (round half away)" : "fixed128 (truncate)";
		std::array<fixed_t, kN> x;
		std::array<std::array<double, 3>, kN> v;
		double gm[kN];
		fixed_t offset;

		void init(const body_data* b, const double (*dx)[3], double off)
		{
			offset = fixed_t{fx128::double4{off, off, off, 0.0}};
			for (int i = 0; i < kN; ++i) {
				gm[i] = b[i].gm;
				x[i] = fixed_t{fx128::double4{b[i].x[0], b[i].x[1], b[i].x[2], 0.0}} + fixed_t{fx128::double4{dx[i][0], dx[i][1], dx[i][2], 0.0}} + offset;
				for (int k = 0; k < 3; ++k) v[i][k] = b[i].v[k];
			}
		}

		void drift(double h)
		{
			for (int i = 0; i < kN; ++i) x[i] += fixed_t{fx128::double4{v[i][0] * h, v[i][1] * h, v[i][2] * h, 0.0}};
		}

		void kick(double h)
		{
			kick_double(v, gm, h, [&](int i, int j, double* r) {
				const fx128::double4 d = static_cast<fx128::double4>(x[j] - x[i]);
				r[0] = d.x, r[1] = d.y, r[2] = d.z;
			});
		}

		// Exact enough: a 128-bit integer as a sum of four exactly representable 32-bit pieces, then scaled.
		static dd to_dd(uint64_t upper, uint64_t lower)
		{
			const int64_t uh = static_cast<int64_t>(upper) >> 32;
			const dd v = dd(std::ldexp(static_cast<double>(uh), 96)) + dd(std::ldexp(static_cast<double>(upper & 0xFFFFFFFFu), 64)) +
			             dd(std::ldexp(static_cast<double>(lower >> 32), 32)) + dd(static_cast<double>(lower & 0xFFFFFFFFu));
			const double s = std::ldexp(1.0, -kFracBits);
			return {v.hi * s, v.lo * s};
		}

		snapshot snap() const
		{
			snapshot s;
			for (int i = 0; i < kN; ++i) {
				const fixed_t p = x[i] - offset;
				const uint64_t u[3] = {p.upper.x, p.upper.y, p.upper.z}, l[3] = {p.lower.x, p.lower.y, p.lower.z};
				for (int k = 0; k < 3; ++k) {
					s.x[i][k] = to_dd(u[k], l[k]);
					s.v[i][k] = v[i][k];
				}
			}
			return s;
		}
	};

	// ---- running and measuring ----

	struct run_result
	{
		std::vector<snapshot> snaps; // at the checkpoints
		double ns_per_step = 0;
	};

	template <class Variant>
	run_result run(const std::vector<substep>& scheme, double dt, const std::vector<long long>& checkpoints, const double (*dx)[3], double offset)
	{
		Variant s;
		s.init(kBodies, dx, offset);
		run_result r;
		r.snaps.push_back(s.snap());
		long long step = 0;
		double seconds = 0;
		for (long long target : checkpoints) {
			const auto t0 = std::chrono::steady_clock::now();
			for (; step < target; ++step)
				for (const substep& sub : scheme) sub.drift ? s.drift(sub.c * dt) : s.kick(sub.c * dt);
			seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			r.snaps.push_back(s.snap());
		}
		r.ns_per_step = step ? seconds * 1e9 / static_cast<double>(step) : 0;
		return r;
	}

	dd energy(const snapshot& s)
	{
		dd e = 0;
		for (int i = 0; i < kN; ++i) {
			const dd v2 = s.v[i][0] * s.v[i][0] + s.v[i][1] * s.v[i][1] + s.v[i][2] * s.v[i][2];
			e += dd(0.5) * dd(kBodies[i].gm) * v2;
			for (int j = i + 1; j < kN; ++j) {
				vec_dd r;
				for (int k = 0; k < 3; ++k) r[k] = s.x[j][k] - s.x[i][k];
				e -= dd(kBodies[i].gm) * dd(kBodies[j].gm) / sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
			}
		}
		return e; // energy * G (the masses are given as GM)
	}

	double distance(const vec_dd& a, const vec_dd& b)
	{
		const dd d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
		return std::sqrt((d0 * d0 + d1 * d1 + d2 * d2).to_double());
	}

	struct metrics
	{
		double pos_err = 0; // max over bodies of |x - x_ref| (m)
		double rel_err = 0; // max over the moons of |(x_moon - x_Jupiter) - reference| (m)
		double energy_err = 0; // |E - E_ref(t)| / |E_ref(0)|: the integrator's own energy error cancels, rounding remains
	};

	metrics measure(const snapshot& s, const snapshot& ref, const dd& e0)
	{
		metrics m;
		for (int i = 0; i < kN; ++i) m.pos_err = std::max(m.pos_err, distance(s.x[i], ref.x[i]));
		for (int i = 2; i < kN; ++i) {
			vec_dd a, b;
			for (int k = 0; k < 3; ++k) a[k] = s.x[i][k] - s.x[kJupiter][k], b[k] = ref.x[i][k] - ref.x[kJupiter][k];
			m.rel_err = std::max(m.rel_err, distance(a, b));
		}
		m.energy_err = std::fabs(((energy(s) - energy(ref)) / e0).to_double());
		return m;
	}

	// Least-squares slope of log(err) over log(t), over the second half of the checkpoints: ~0.5 for an unbiased
	// random walk of the rounding errors (Brouwer's law), ~1 for a systematic drift.
	double growth_exponent(const std::vector<double>& t, const std::vector<double>& err)
	{
		double sx = 0, sy = 0, sxx = 0, sxy = 0;
		int n = 0;
		for (size_t i = t.size() / 2; i < t.size(); ++i) {
			if (err[i] <= 0 || t[i] <= 0) continue;
			const double x = std::log(t[i]), y = std::log(err[i]);
			sx += x, sy += y, sxx += x * x, sxy += x * y, ++n;
		}
		if (n < 3) return NAN;
		return (n * sxy - sx * sy) / (n * sxx - sx * sx);
	}
}

int main(int argc, char** argv)
{
	double years = 100, offset = 0;
	std::string scheme_name = "fr4";
	int steps_per_orbit = 0, runs = 1;
	bool check = false;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		auto next = [&] { return i + 1 < argc ? argv[++i] : "0"; };
		if (a == "--years") years = std::atof(next());
		else if (a == "--integrator") scheme_name = next();
		else if (a == "--steps-per-orbit") steps_per_orbit = std::atoi(next());
		else if (a == "--offset") offset = std::atof(next());
		else if (a == "--runs") runs = std::atoi(next());
		else if (a == "--check") check = true;
		else {
			std::fprintf(stderr, "unknown argument %s\n", a.c_str());
			return 2;
		}
	}
	if (!steps_per_orbit) steps_per_orbit = scheme_name == "leapfrog" ? 2048 : 256;
	const std::vector<substep> scheme = integrator(scheme_name);
	const double dt = kIoPeriod / steps_per_orbit;
	const long long total_steps = static_cast<long long>(years * kYear / dt);

	// Log-spaced checkpoints from about one day to the end.
	std::vector<long long> checkpoints;
	for (double t = 86400; t < years * kYear * 1.0001; t *= 1.5) {
		const long long s = std::max(1LL, static_cast<long long>(t / dt));
		if (checkpoints.empty() || s > checkpoints.back()) checkpoints.push_back(std::min(s, total_steps));
	}
	if (checkpoints.empty() || checkpoints.back() != total_steps) checkpoints.push_back(total_steps);

	std::printf("# Orbit drift: Sun, Jupiter, Io, Europa, Ganymede, Callisto\n\n");
	std::printf("integrator %s, dt %.3f s (%d steps per Io orbit), %.3g years = %lld steps, offset %.3g m, fixed128 frac bits %d, %d run(s)\n\n",
		scheme_name.c_str(), dt, steps_per_orbit, years, total_steps, offset, kFracBits, runs);

	struct summary
	{
		const char* name;
		std::vector<double> pos, rel, energy, slope, ns;
	};
	summary sums[] = {{double_variant<false>::name, {}, {}, {}, {}, {}}, {double_variant<true>::name, {}, {}, {}, {}, {}},
		{fixed_variant::name, {}, {}, {}, {}, {}}};
	double ref_ns = 0;

	for (int run_index = 0; run_index < runs; ++run_index) {
		// Tiny, run-dependent perturbation of the initial positions (identical for every variant and the reference).
		double dx[kN][3] = {};
		for (int i = 0; i < kN; ++i)
			for (int k = 0; k < 3; ++k) dx[i][k] = 1e-3 * run_index * ((i * 3 + k) % 5 - 2);

		const run_result ref = run<reference_variant>(scheme, dt, checkpoints, dx, offset);
		ref_ns += ref.ns_per_step / runs;
		const dd e0 = energy(ref.snaps[0]);
		const run_result results[] = {run<double_variant<false>>(scheme, dt, checkpoints, dx, offset),
			run<double_variant<true>>(scheme, dt, checkpoints, dx, offset), run<fixed_variant>(scheme, dt, checkpoints, dx, offset)};

		if (run_index == 0) {
			std::printf("## Run 1: error vs the double-double reference over time\n\n");
			std::printf("| time (years) | variant | max position error (m) | max moon-Jupiter error (m) | relative energy error |\n|---|---|---|---|---|\n");
			for (size_t c = 0; c < checkpoints.size(); c += std::max<size_t>(1, checkpoints.size() / 8)) {
				for (int v = 0; v < 3; ++v) {
					const metrics m = measure(results[v].snaps[c + 1], ref.snaps[c + 1], e0);
					std::printf("| %.4g | %s | %.3g | %.3g | %.3g |\n", checkpoints[c] * dt / kYear, sums[v].name, m.pos_err, m.rel_err, m.energy_err);
				}
			}
			const dd e_end = energy(ref.snaps.back());
			std::printf("\nintegrator energy error of the reference at the end (truncation, not rounding; cancels in the table): %.3g\n\n",
				std::fabs(((e_end - e0) / e0).to_double()));
		}

		for (int v = 0; v < 3; ++v) {
			std::vector<double> t, e;
			for (size_t c = 0; c < checkpoints.size(); ++c) {
				t.push_back(checkpoints[c] * dt);
				e.push_back(measure(results[v].snaps[c + 1], ref.snaps[c + 1], e0).energy_err);
			}
			const metrics m = measure(results[v].snaps.back(), ref.snaps.back(), e0);
			sums[v].pos.push_back(m.pos_err);
			sums[v].rel.push_back(m.rel_err);
			sums[v].energy.push_back(m.energy_err);
			sums[v].slope.push_back(growth_exponent(t, e));
			sums[v].ns.push_back(results[v].ns_per_step);
		}
	}

	auto median = [](std::vector<double> v) {
		std::sort(v.begin(), v.end());
		return v.empty() ? NAN : v[v.size() / 2];
	};
	std::printf("## Summary at %.3g years (median over %d run(s))\n\n", years, runs);
	std::printf("| variant | ns per step | max position error (m) | max moon-Jupiter error (m) | relative energy error | energy error growth exponent |\n|---|---|---|---|---|---|\n");
	for (const summary& s : sums)
		std::printf("| %s | %.0f | %.3g | %.3g | %.3g | %.2f |\n", s.name, median(s.ns), median(s.pos), median(s.rel), median(s.energy), median(s.slope));
	std::printf("| %s | %.0f | 0 | 0 | 0 | |\n", reference_variant::name, ref_ns);
	std::printf("\nGrowth exponent: ~0.5 = unbiased rounding (random walk), ~1 = systematic drift.\n");

	if (check) {
		// fixed128 positions must not be less accurate than double positions (with a margin for rounding noise).
		const double fixed_err = median(sums[2].pos), double_err = median(sums[0].pos);
		if (!(fixed_err <= 2 * double_err + 1e-6)) {
			std::printf("CHECK FAILED: fixed128 position error %.3g m > double position error %.3g m\n", fixed_err, double_err);
			return 1;
		}
		std::printf("check passed\n");
	}
	return 0;
}
