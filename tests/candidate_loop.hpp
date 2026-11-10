#pragma once
#include "test_helpers.hpp"

#include <candidates/candidates.hpp>

#include <string>

// Runs body(candidate type tag, scale) for every registered candidate x every tested scale.
template <class Body>
void for_each_candidate_and_scale(Body&& body)
{
	candidates::for_each_type<candidates::all>([&](auto candidate) {
		candidates::for_each_type<test_frac_bits>([&](auto scale) { body(candidate, scale); });
	});
}

template <class C, int F>
std::string config_name()
{
	return std::string(C::name) + ", frac bits " + std::to_string(F);
}

template <class C>
ref::rounding rounding_of()
{
	return C::round_half_away ? ref::rounding::half_away_from_zero : ref::rounding::trunc;
}
