#pragma once
#include <cstdint>
#include <utility>

template<typename ID>
struct TypesafeHandle
{
	constexpr TypesafeHandle() = default;
	static constexpr TypesafeHandle<ID> invalidHandle() { return TypesafeHandle<ID>{~0U}; };
	bool operator!=(TypesafeHandle<ID> h) const
	{
		return h.handle != handle;
	}
	bool operator==(TypesafeHandle<ID> h) const
	{
		return h.handle == handle;
	}

	auto operator<=>(TypesafeHandle<ID> h) const
	{
		return h.handle <=> handle;
	}

	static TypesafeHandle<ID> getNextHandle()
	{
		return TypesafeHandle<ID>(nextHandle++);
	}

	operator uint64_t() const { return handle; }

private:
	explicit constexpr TypesafeHandle(uint64_t givenHandle) : handle(givenHandle) {}
	uint64_t handle{};
	static uint64_t nextHandle;
};

template<typename ID>
uint64_t TypesafeHandle<ID>::nextHandle = { 1 };

namespace std
{
	template<typename ID>
	struct hash<TypesafeHandle<ID>>
	{
		size_t operator()(TypesafeHandle<ID> handle) const
		{
			//handles are unique, so hashing would be a waste of time
			return (size_t)static_cast<uint64_t>(handle);
		}
	};
}