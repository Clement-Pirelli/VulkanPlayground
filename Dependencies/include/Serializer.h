#pragma once
#include <cstdint>
#include <cassert>

class StreamIn
{
public:

	StreamIn(std::byte *givenData, size_t givenMaxSize) :data(givenData), maxSize(givenMaxSize) {}

	
	template<typename T>
	[[nodiscard]]
	T getNext() noexcept {
		size_t oldAt = at;
		at += sizeof(T);

		return deserialize<T>(data + oldAt, maxSize - oldAt);
	}

	template<typename T>
	[[nodiscard]]
	void getNext(T *dataToFill, size_t amount) noexcept
	{
		for (size_t i = 0; i < amount; i++)
		{
			size_t oldAt = at;
			at += sizeof(T);

			dataToFill[i] = deserialize<T>(data + oldAt, maxSize - oldAt);
		}
	}

	template<typename T>
	[[nodiscard]]
	T peekNext() const noexcept {
		uint8_t *address = data + at;
		return deserialize<T>(address, maxSize - at);
	}

	size_t bytesRead() const noexcept { return at; }

private:

	template<typename T>
	static T deserialize(std::byte *data, [[maybe_unused]] size_t size) noexcept
	{
		assert(size >= sizeof(T));

		return *(reinterpret_cast<T *>(data));
	}

	std::byte *data = nullptr;
	size_t at = 0U;
	size_t maxSize = 0U;
};


class StreamOut
{
public:

	StreamOut(std::byte *givenData, size_t givenMaxSize) : data(givenData), maxSize(givenMaxSize) {}

	template<typename T>
	void setNext(const T &item) noexcept
	{
		std::byte *address = data + at;
		at += sizeof(T);
		memcpy(address, &item, sizeof(T));
	}

	template<typename T>
	void setNext(T *items, size_t amount) noexcept
	{
		assert(items != nullptr);
		std::byte* address = data + at;
		at += sizeof(T) * amount;
		memcpy(address, items, sizeof(T) * amount);
	}

	[[nodiscard]]
	std::byte *getData() const noexcept
	{
		return data;
	}

	[[nodiscard]]
	size_t bytesWritten() const noexcept { return at; }

private:
	std::byte *data = nullptr;
	size_t at = 0U;
	size_t maxSize = 0U;
};

#include <vector>
class StretchyStreamOut
{
public:

	StretchyStreamOut(size_t expectedSize = 0) : data(expectedSize) {}

	template<typename T>
	void setNext(const T &item) noexcept
	{
		const size_t at = data.size();
		const size_t size = sizeof(T);
		data.resize(data.size()+size);
		memcpy(data.data()+at, &item, size);
	}

	template<typename T>
	void setNext(T *items, size_t amount) noexcept
	{
		assert(items != nullptr);
		const size_t at = data.size();
		const size_t size = sizeof(T) * amount;
		data.resize(data.size() + size);
		memcpy(data.data(), items, size);
	}

	[[nodiscard]]
	const std::byte *getData() const noexcept
	{
		return data.data();
	}

	[[nodiscard]]
	size_t bytesWritten() const noexcept { return data.size(); }

private:

	std::vector<std::byte> data;
};