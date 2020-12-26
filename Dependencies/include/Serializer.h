#pragma once
#include <cstdint>
#include <cassert>

class StreamIn
{
public:

	StreamIn(uint8_t *givenData, size_t givenMaxSize) :data(givenData), maxSize(givenMaxSize) {}

	
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

	size_t bytesWritten() const noexcept { return at; }

private:

	template<typename T>
	static T deserialize(uint8_t *data, [[maybe_unused]] size_t size) noexcept
	{
		assert(size >= sizeof(T));

		return *(reinterpret_cast<T *>(data));
	}

	uint8_t *data = nullptr;
	size_t at = 0U;
	size_t maxSize = 0U;
};


class StreamOut
{
public:

	StreamOut(uint8_t *givenData, size_t givenMaxSize) : data(givenData), maxSize(givenMaxSize) {}

	template<typename T>
	void setNext(const T &item) noexcept
	{
		uint8_t *address = data + at;
		at += sizeof(T);
		memcpy(address, &item, sizeof(T));
	}

	template<typename T>
	void setNext(T *items, size_t amount) noexcept
	{
		assert(items != nullptr);
		uint8_t *address = data + at;
		at += sizeof(T) * amount;
		memcpy(address, items, sizeof(T) * amount);
	}

	[[nodiscard]]
	uint8_t *getData() const noexcept
	{
		return data;
	}

	[[nodiscard]]
	size_t bytesWritten() const noexcept { return at; }

private:
	uint8_t *data = nullptr;
	size_t at = 0U;
	size_t maxSize = 0U;
};