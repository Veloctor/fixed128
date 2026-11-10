#pragma once

// fixed128: fast conversions between double and 128-bit fixed-point numbers, 4 lanes at a time
// (AVX2 backend on x86-64, portable scalar backend elsewhere; see config.hpp).
// Include this header to get the whole public API (namespace fx128).

#include "config.hpp"
#include "check.hpp"
#include "types.hpp"
#include "convert.hpp"
#include "scalar.hpp"
#include "simd.hpp"
