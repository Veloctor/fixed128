#pragma once
#include "../config.hpp"

// Selects the vector primitives of the configured backend (see config.hpp).
#if FIXED128_BACKEND_AVX2
#include "avx2/native.hpp"
#else
#include "scalar/native.hpp"
#endif
