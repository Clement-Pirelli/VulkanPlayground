#pragma once
#include "ResourceMap.h"
#include <optional>
#include <memory>
#include <string>

template<typename T>
class ConsoleVariables
{
public:
	static std::optional<T> get(const std::string &name) 
	{
		T* result = getInstance().values.get(name);
		if(result == nullptr)
		{
			return std::nullopt;
		}
		else 
		{
			return *result;
		}
	}

	static void set(const std::string& hashedName, T value)
	{
		getInstance().values.set(hashedName, value);
	}

	template<typename Operation_t>
	static void forEach(Operation_t operation)
	{
		getInstance().values.forEach(operation);
	}

private:
	static ConsoleVariables &getInstance()
	{
		static ConsoleVariables instance;
		return instance;
	}
	ResourceMap<std::string, T> values;
};

template<typename T>
class ConsoleVariable
{
public:
	constexpr ConsoleVariable(std::string givenName, T givenValue) : name(givenName)
	{
		ConsoleVariables<T>::set(givenName, givenValue);
	}

	T get()
	{
		std::optional<T> result = ConsoleVariables<T>::get(name);
		if (!result.has_value()) 
		{
			std::terminate(); //this should never, ever happen, but it might if something gets real bad
		}
		return result.value();
	}

	void set(const T& newValue)
	{
		ConsoleVariables<T>::set(name, newValue);
	}

private:
	std::string name{};
};

