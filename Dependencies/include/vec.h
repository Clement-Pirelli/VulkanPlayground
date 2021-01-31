#pragma once
#include <array>

template<typename T, size_t N>
struct vec
{
	static_assert(N != 0);

#pragma region macro definitions
#define COMPOUND_VEC_TO_VEC_OPERATOR(op) vec<T, N>& operator op(const vec<T, N> &other)	\
	{																				\
		for (size_t i = 0; i < N; i++)												\
		{																			\
			e[i] op other[i];														\
		}																			\
		return *this;																\
	}

#define COMPOUND_VEC_TO_FLOAT_OPERATOR(op) vec<T, N>& operator op(T other)			\
	{																				\
		for (size_t i = 0; i < N; i++)												\
		{																			\
			e[i] op other;															\
		}																			\
		return *this;																\
	}

#define VEC_TO_VEC_OPERATOR(op) vec<T, N> operator op(const vec<T, N> &other) const	\
	{																				\
		vec<T, N> result = {};															\
		for (size_t i = 0; i < N; i++)												\
		{																			\
			result[i] = e[i] op other[i];											\
		}																			\
		return result;																\
	}

#define VEC_TO_FLOAT_OPERATOR(op) vec<T, N> operator op(T other) const					\
	{																				\
		vec<T, N> result = {};															\
		for (size_t i = 0; i < N; i++)												\
		{																			\
			result[i] = e[i] op other;												\
		}																			\
		return result;																\
	}

#define VEC_ELEMENT_GETTER(elementN, name)											\
	T name() const requires (N >= elementN) { return e[elementN]; };			\
	T& name() requires (N >= elementN) { return e[elementN]; };

#pragma endregion

	vec() = default;

	T operator[](size_t i) const { return e[i]; }
	T &operator[](size_t i) { return e[i]; }
	
	bool operator==(vec<T, N> other) const
	{
		for (size_t i = 0; i < N; i++)
			if (e[i] != other[i]) return false;
		return true;
	}

	COMPOUND_VEC_TO_VEC_OPERATOR(+=);
	COMPOUND_VEC_TO_VEC_OPERATOR(-=);
	COMPOUND_VEC_TO_VEC_OPERATOR(/=);
	COMPOUND_VEC_TO_VEC_OPERATOR(*=);

	COMPOUND_VEC_TO_FLOAT_OPERATOR(+=);
	COMPOUND_VEC_TO_FLOAT_OPERATOR(-=);
	COMPOUND_VEC_TO_FLOAT_OPERATOR(/=);
	COMPOUND_VEC_TO_FLOAT_OPERATOR(*=);

	VEC_TO_VEC_OPERATOR(+);
	VEC_TO_VEC_OPERATOR(-);
	VEC_TO_VEC_OPERATOR(/);
	VEC_TO_VEC_OPERATOR(*);

	VEC_TO_FLOAT_OPERATOR(+);
	VEC_TO_FLOAT_OPERATOR(-);
	VEC_TO_FLOAT_OPERATOR(/);
	VEC_TO_FLOAT_OPERATOR(*);

	VEC_ELEMENT_GETTER(0, x);
	VEC_ELEMENT_GETTER(1, y);
	VEC_ELEMENT_GETTER(2, z);
	VEC_ELEMENT_GETTER(3, w);

	VEC_ELEMENT_GETTER(0, r);
	VEC_ELEMENT_GETTER(1, g);
	VEC_ELEMENT_GETTER(2, b);
	VEC_ELEMENT_GETTER(3, a);

	T length() const 
	{
		return (T)sqrtf((float)squaredLength());
	}

	T squaredLength() const
	{
		return dot(*this, *this);
	}

	void normalize() 
	{
		*this = *this / length();
	}

	vec<T, N> normalized() const
	{
		return *this / length();
	}

	vec<T, N> clampedBy(vec<T, N> min, vec<T, N> max) const
	{
		vec<T, N> copy = *this;
		for (size_t i = 0; i < N; i++)
		{
			if (copy.e[i] < min.e[i]) copy.e[i] = min.e[i];
			if (copy.e[i] > max.e[i]) copy.e[i] = max.e[i];
		}
		return copy;
	}

	vec<T, N>& saturate()
	{
		for(size_t i = 0; i < N; i++)
		{
			e[i] = e[i] > T{ 1 } ? T{ 1 } : e[i];
			e[i] = e[i] < T{ 0 } ? T{ 0 } : e[i];
		}

		return *this;
	}

	static vec<T, N> lerp(const vec<T, N> &v1, const vec<T, N> &v2, T t)
	{
		return v1 * (T{1} - t) + v2 * t;
	}

	vec<T, 2> xy() const requires (N >= 3)
	{
		return vec<T, 2>(x(), y());
	};

	vec<T, 3> xyz() const requires (N >= 4)
	{
		return vec<T, 3>(x(), y(), z());
	};

	vec(T givenX, T givenY) requires (N == 2)
	{
		e[0] = givenX;
		e[1] = givenY;
	};

	vec(T givenX, T givenY, T givenZ) requires (N == 3)
	{
		e[0] = givenX;
		e[1] = givenY;
		e[2] = givenZ;
	};

	vec(T givenX, T givenY, T givenZ, T givenW) requires (N == 4)
	{
		e[0] = givenX;
		e[1] = givenY;
		e[2] = givenZ;
		e[3] = givenW;
	};

	static T dot(const vec<T, N> &a, const vec<T, N> &b)
	{
		T result{0};
		for (size_t i = 0; i < N; i++)
		{
			result += a[i] * b[i];
		}
		return result;
	}

	static vec<T, 3> cross(const vec<T, 3> &v1, const vec<T, 3> &v2) requires (N == 3)
	{
		return vec<T, 3>({
			v1.y() * v2.z() - v1.z() * v2.y(),
			v1.z() * v2.x() - v1.x() * v2.z(),
			v1.x() * v2.y() - v1.y() * v2.x()
			});
	};
	
	static bool refract(const vec<T, N> &incident, const vec<T, N> &normal, float niOverNt, vec<T, N> &refracted) requires (N == 3)
	{
		vec<T, 3> uv = incident.normalized();
		T dt = dot(uv, normal);
		float discriminant = 1.0f - niOverNt * niOverNt * (1.0f - (float)(dt * dt));
		if (discriminant > .0f)
		{
			refracted = (uv - normal * dt) * niOverNt - normal * sqrtf(discriminant);
			return true;
		}
		return false;
	};

	static vec<T, 4> fromPoint(const vec<T, 3> &point) requires (N == 4)
	{
		return vec<T, 4>(point.x(), point.y(), point.z(), T{1});
	};

	static vec<T, 4> fromDirection(const vec<T, 3> &direction) requires (N == 4)
	{
		return vec<T, 4>(direction.x(), direction.y(), direction.z(), T{0});
	};

	vec<T, 3> rotatedX(float angle) const requires (N == 3)
	{
		return vec<T, 3>(x(), y() * cosf(angle) - z * sinf(angle), y() * sinf(angle) + z() * cosf(angle));
	};

	vec<T, 3> rotatedY(float angle) const requires (N == 3)
	{
		return vec<T, 3>(x() * cosf(angle) + z() * sinf(angle), y(), -x() * sinf(angle) + z() * cosf(angle));
	};

	vec<T, 3> rotatedZ(float angle) const requires (N == 3)
	{
		return vec<T, 3>(x() * cosf(angle) - y() * sinf(angle), x() * sinf(angle) + y() * cosf(angle), z());
	};

	static vec<T, 3> reflect(const vec<T, 3> &incident, const vec<T, 3> &normal) requires (N == 3)
	{
		return incident - (normal * dot(incident, normal) * T{2});
	};

	static constexpr size_t size()
	{
		return N;
	}

	std::array<T, N> e = {};
};

namespace std
{
	template<typename T, size_t N> struct hash<vec<T, N>>
	{
		size_t operator()(const vec<T, N> &v)
		{
			//from: https://github.com/g-truc/glm/blob/master/glm/gtx/hash.inl
			auto combine = [](size_t seed, size_t hash) { return seed ^= (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2)); };
			
			size_t result{};
			hash<T> hasher{};
			for (size_t i = 0U; i < N; i++)
			{
				result = combine(result, hasher(v[i]));
			}
			return result;
		}
	};
}

using vec4 = vec<float, 4>;
using vec3 = vec<float, 3>;
using vec2 = vec<float, 2>;

using ivec4 = vec<int, 4>;
using ivec3 = vec<int, 3>;
using ivec2 = vec<int, 2>;

#undef VEC_ELEMENT_GETTER
#undef COMPOUND_VEC_TO_VEC_OPERATOR
#undef COMPOUND_VEC_TO_FLOAT_OPERATOR
#undef VEC_TO_VEC_OPERATOR
#undef VEC_TO_FLOAT_OPERATOR

