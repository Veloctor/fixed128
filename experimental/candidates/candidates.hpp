#pragma once
// Registry of candidate implementations of the double <-> fixed128 conversions.
// Every candidate listed in `all` is automatically run through the contract tests (tests/test_contract.cpp),
// the accuracy report (tests/accuracy_report.cpp) and the benchmarks (benchmarks/bench_conversions.cpp).
// How to add one: see README.md in this directory.
#include "library.hpp"
#include "scalar_class.hpp"

#include <tuple>
#include <type_traits>

namespace candidates
{
	using all = std::tuple<library, scalar_class>;

	// Which of the four conversions a candidate provides (each is optional).
	template <class C, class = void>
	struct has_double_to_fixed128 : std::false_type {};
	template <class C>
	struct has_double_to_fixed128<C, std::void_t<decltype(&C::template double_to_fixed128<0>)>> : std::true_type {};

	template <class C, class = void>
	struct has_double_to_ufixed128 : std::false_type {};
	template <class C>
	struct has_double_to_ufixed128<C, std::void_t<decltype(&C::template double_to_ufixed128<0>)>> : std::true_type {};

	template <class C, class = void>
	struct has_fixed128_to_double : std::false_type {};
	template <class C>
	struct has_fixed128_to_double<C, std::void_t<decltype(&C::template fixed128_to_double<0>)>> : std::true_type {};

	template <class C, class = void>
	struct has_ufixed128_to_double : std::false_type {};
	template <class C>
	struct has_ufixed128_to_double<C, std::void_t<decltype(&C::template ufixed128_to_double<0>)>> : std::true_type {};

	// The fused operations on fixed128x4 (each optional as well).
	// A candidate may restrict their domain (a "_limited" variant) by declaring
	//   static constexpr int subtract_max_log2 = N;   // subtract_to_double / _float: |a - b| < 2^N units (default 128: all inputs)
	//   static constexpr int add_max_log2 = N;        // add_double: -2^N <= b * 2^F < 2^N (default 127: the conversion's whole domain)
	template <class C, class = void>
	struct has_subtract_to_double : std::false_type {};
	template <class C>
	struct has_subtract_to_double<C, std::void_t<decltype(&C::template subtract_to_double<0>)>> : std::true_type {};

	template <class C, class = void>
	struct has_subtract_to_float : std::false_type {};
	template <class C>
	struct has_subtract_to_float<C, std::void_t<decltype(&C::template subtract_to_float<0>)>> : std::true_type {};

	template <class C, class = void>
	struct has_add_double : std::false_type {};
	template <class C>
	struct has_add_double<C, std::void_t<decltype(&C::template add_double<0>)>> : std::true_type {};

	template <class C, class = void>
	struct subtract_max_log2 : std::integral_constant<int, 128> {};
	template <class C>
	struct subtract_max_log2<C, std::void_t<decltype(C::subtract_max_log2)>> : std::integral_constant<int, C::subtract_max_log2> {};

	template <class C, class = void>
	struct add_max_log2 : std::integral_constant<int, 127> {};
	template <class C>
	struct add_max_log2<C, std::void_t<decltype(C::add_max_log2)>> : std::integral_constant<int, C::add_max_log2> {};

	template <class T>
	struct type_tag
	{
		using type = T;
	};

	// Calls f(type_tag<T>{}) for every type T of a std::tuple.
	template <class... Ts, class Fn>
	void for_each_type(std::tuple<Ts...>*, Fn&& f)
	{
		(f(type_tag<Ts>{}), ...);
	}

	template <class Tuple, class Fn>
	void for_each_type(Fn&& f)
	{
		for_each_type(static_cast<Tuple*>(nullptr), f);
	}
}
