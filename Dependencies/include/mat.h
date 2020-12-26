#pragma once
#include <array>
#include "vec.h"
#include <math.h>

template<size_t N>
struct squaremat;

using mat4x4 = squaremat<4>;
using mat3x3 = squaremat<3>;


template<size_t N>
struct squaremat
{
	squaremat() { elements = identity().elements; }
	squaremat(const std::array<float, N *N> &arr)
	{
		elements = arr;
	}
	squaremat(std::array<float, N *N> &&arr)
	{
		elements = arr;
	}

	inline float operator[](size_t i) const { return elements[i]; }
	inline float &operator[](size_t i) { return elements[i]; }
	inline float at(size_t x, size_t y) const { return elements[x + y * N]; }
	inline float &at(size_t x, size_t y) { return elements[x + y * N]; }

	inline vec<N> columnAt(size_t x) const
	{
		vec<N> result = {};
		for (size_t i = 0; i < N; i++)
		{
			result[i] = at(x, i);
		}
		return result;
	}

	inline vec<N> rowAt(size_t y) const
	{
		vec<N> result = {};
		for (size_t i = 0; i < N; i++)
		{
			result[i] = at(i, y);
		}
		return result;
	}

	inline squaremat<N> operator *(const squaremat<N> &other) const
	{
		squaremat<N> result = {};

		for (size_t y = 0; y < N; y++)
			for (size_t x = 0; x < N; x++)
			{
				result.at(x, y) = vec<N>::dot(rowAt(y), other.columnAt(x));
			}

		return result;
	}

	vec<N> operator *(const vec<N> &v) const
	{
		vec<N> result = {};
		for (size_t rowIndex = 0; rowIndex < N; rowIndex++)
		{
			for (size_t elementIndex = 0; elementIndex < N; elementIndex++)
			{
				result[rowIndex] += v[elementIndex] * at(elementIndex, rowIndex);
			}
		}

		return result;
	}

	squaremat<N> operator *(const float value) const
	{
		squaremat<N> result = *this;
		for (auto &element : result.elements)
		{
			element *= value;
		}
		return result;
	}

	squaremat<N> operator /(const float value) const
	{
		squaremat<N> result = *this;
		for (auto &element : result.elements)
		{
			element /= value;
		}
		return result;
	}

	squaremat<N> transposed() const
	{
		squaremat<N> copy = *this;

		for (size_t y = 0; y < N; y++)
			for (size_t x = 0; x < N; x++)
			{
				copy.at(x, y) = at(y, x);
			}

		return copy;
	}

	constexpr static squaremat<N> identity()
	{
		std::array<float, N *N> resultArr = {};
		for (size_t y = 0; y < N; y++)
			for (size_t x = 0; x < N; x++)
			{
				resultArr[x + y * N] = (x == y) ? 1.0f : .0f;
			}

		return squaremat<N>(resultArr);
	}

	static squaremat<N> rotatedX(float byRadians) requires (N >= 3)
	{
		return mat3x3({
			1.0f, .0f,			 .0f,
			 .0f, cosf(byRadians), -sinf(byRadians),
			 .0f, sinf(byRadians), cosf(byRadians)
			}).expandTo<N>();
	};

	static squaremat<N> rotatedY(float byRadians) requires (N >= 3)
	{
		return mat3x3({
			cosf(byRadians), .0f, sinf(byRadians),
			.0f,			1.0f, .0f,
			-sinf(byRadians),.0f, cosf(byRadians)
			}).expandTo<N>();
	};

	static squaremat<N> rotateZ(float byRadians) requires (N >= 3)
	{
		return mat3x3({
			cosf(byRadians), -sinf(byRadians),	 .0f,
			sinf(byRadians), cosf(byRadians),	 .0f,
			.0f,			.0f,				1.0f
			}).expandTo<N>();
	};

	static mat4x4 translate(const vec3 &by) requires (N == 4)
	{
		return mat4x4({
			1.0f, .0f, .0f, by.x(),
			.0f, 1.0f, .0f, by.y(),
			.0f, .0f, 1.0f,	by.z(),
			.0f, .0f, .0f , 1.0f
			});
	};

	static squaremat<N> scale(const vec3 &by) requires (N >= 3)
	{
		return mat3x3({
			by.x(), .0f, .0f,
			.0f, by.y(), .0f,
			.0f, .0f, by.z()
			}).expandTo<N>();
	};

	struct ViewportDescription
	{
		float x = 0, y = 0;
		size_t width = 0, height = 0;
	};
	static mat4x4 viewport(const ViewportDescription &viewport) requires (N == 4)
	{
		const float halfWidth = static_cast<float>(viewport.width) / 2.0f;
		const float halfHeight = static_cast<float>(viewport.height) / 2.0f;
		return mat4x4({
			halfWidth, .0f,.0f,viewport.x + halfWidth,
			.0f, halfHeight, .0f, viewport.y + halfHeight,
			.0f,.0f,1.0f,.0f,
			.0f,.0f,.0f,1.0f
			});
	};

	template<size_t M>
	squaremat<M> expandTo() requires(M >= N)
	{
		if constexpr (M == N)
		{
			return *this;
		}
		else
		{
			squaremat<M> result = squaremat<M>::identity();

			for (size_t y = 0; y < N; y++)
				for (size_t x = 0; x < N; x++)
				{
					result.at(x, y) = at(x, y);
				}

			return result;
		}
	};

	squaremat<N - 1> calculateMinorAt(size_t row, size_t column) const requires (N >= 2)
	{
		squaremat<N - 1> minor = {};
		for (size_t y = 0; y < N; y++)
			for (size_t x = 0; x < N; x++)
			{
				if (x == row || y == column) continue;

				bool afterRow = x > row;
				bool afterColumn = y > column;

				minor.at(x - afterRow, y - afterColumn) = at(x, y);
			}
		return minor;
	};

	float calculateDeterminant() const
	{
		float result = 0;

		if constexpr (N > 2U) {
			for (size_t i = 0; i < N; i++) {
				float lowerDet = calculateMinorAt(0, i).calculateDeterminant();
				const bool adding = i % 2 == 0;

				result += at(0, i) * (adding ? 1.0f : -1.0f) * lowerDet;
			}
		}
		else {
			result = at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0);
		}

		return result;
	}

	squaremat<N> calculateAdjugate() const
	{
		squaremat<N> adjugate = {};

		for (size_t y = 0; y < N; y++)
			for (size_t x = 0; x < N; x++)
			{
				const bool positive = ((x + y) % 2) == 0;
				adjugate.at(y, x) = (positive ? 1.0f : -1.0f) * calculateMinorAt(x, y).calculateDeterminant();
			}

		return adjugate;
	}

	squaremat<N> inversed() const
	{
		const float determinant = calculateDeterminant();
		const squaremat<N> adjugate = calculateAdjugate();

		return adjugate / determinant;
	}

	struct PerspectiveProjection
	{
		float fovX = {}, aspectRatio = {}, zfar = {}, znear = {};
	};
	static mat4x4 perspective(const PerspectiveProjection &projection) requires (N == 4)
	{
		const float fovY = projection.fovX * projection.aspectRatio;
		const float halfFovY = fovY * .5f;
		const float halfFovX = projection.fovX * .5f;

		const float m33 = -((projection.zfar + projection.znear) / (projection.zfar - projection.znear));
		const float m43 = -(2.0f * (projection.zfar * projection.znear) / (projection.zfar - projection.znear));

		return mat4x4({
			1.0f / atanf(halfFovX), .0f, .0f, .0f,
			.0f, 1.0f / atanf(halfFovY), .0f, .0f,
			.0f, .0f, m33, m43,
			.0f, .0f, -1.0f, 1.0f
			});
	};

	struct OrthographicProjection
	{
		float right, left, top, bottom, far, near;
	};
	static mat4x4 orthographic(const OrthographicProjection &projection) requires (N == 4)
	{
		const float m00 = 2.0f / (projection.right - projection.left);
		const float m40 = -(projection.right + projection.left) / (projection.right - projection.left);
		const float m11 = 2.0f / (projection.top - projection.bottom);
		const float m41 = -(projection.top + projection.bottom) / (projection.top - projection.bottom);
		const float m33 = -2.0f / (projection.far - projection.near);
		const float m43 = -(projection.far + projection.near) / (projection.far - projection.near);
		return mat4x4({
			m00, .0f, .0f, m40,
			.0f, m11, .0f, m41,
			.0f, .0f, m33, m43,
			.0f, .0f, .0f, 1.0f
			});
	}

	struct LookAt
	{
		vec3 eye, target, up;
	};
	static mat4x4 lookAt(const LookAt&params) requires (N == 4)
	{
		const vec3 forward = (params.target - params.eye).normalized();
		const vec3 u = vec3::cross(params.up, forward);
		const vec3 v = vec3::cross(forward, u);

		return mat4x4({
			u.x(), u.y(), u.z(), vec3::dot(params.eye, u),
			v.x(), v.y(), v.z(), vec3::dot(params.eye, v),
			forward.x(), forward.y(), forward.z(), vec3::dot(params.eye, forward),
			.0f, .0f, .0f, 1.0f
			});
	}
	
	std::array<float, N*N> elements = {};
};
