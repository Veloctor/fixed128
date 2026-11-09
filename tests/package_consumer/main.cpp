#include <fixed128/fixed128.hpp>

int main()
{
	const fx128::fixed128x4 a{3.0}, b{1.0};
	alignas(32) double d[4];
	_mm256_store_pd(d, static_cast<fx128::double4>(a - b));
	return d[0] == 2.0 ? 0 : 1;
}
