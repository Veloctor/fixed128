#include <fixed128/fixed128.hpp>

int main()
{
	const fx128::fixed128x4<36> a{3.0}, b{1.0};
	const fx128::double4 d = static_cast<fx128::double4>(a - b);
	return d.x == 2.0 && d.w == 2.0 ? 0 : 1;
}
