#pragma once
namespace math
{
	constexpr float pi = 3.1415926524f;
	constexpr float radToDeg(float n) { return n * 180.0f / pi; }
	constexpr float degToRad(float n) { return n * pi / 180.0f; }

	template <class T>
	T min(const T &a, const T &b)
	{
		return (a < b) ? a : b;
	}

	template <class T>
	T max(const T &a, const T &b)
	{
		return (a > b) ? a : b;
	}
}