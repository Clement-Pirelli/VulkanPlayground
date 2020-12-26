#include "Time.h"
#include <chrono>

Time::Time(milliseconds originalAmount) : ticks(originalAmount)
{
}

//https://stackoverflow.com/questions/31255486/c-how-do-i-convert-a-stdchronotime-point-to-long-and-back
Time::milliseconds Time::milliseconds_since_epoch()
{
	auto now = std::chrono::system_clock::now();
	auto millisecondsNow = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
	auto epoch = millisecondsNow.time_since_epoch();
	return { epoch.count() };
}

float Time::seconds_since_epoch()
{
	return ((float)milliseconds_since_epoch().amount) / 1000.0f;
}

Time Time::now()
{
	return Time({ milliseconds_since_epoch().amount }) - startTime;
}

float Time::asSeconds() const
{
	return ((float)asMilliseconds().amount) / 1000.0f;
}

Time::milliseconds Time::asMilliseconds() const
{
	return ticks;
}

Time Time::operator-(const Time &other) const
{
	return Time({ ticks.amount - other.ticks.amount });
}

void Time::operator-=(const Time &other)
{
	ticks.amount -= other.ticks.amount;
}

void Time::operator+=(const Time &other)
{
	ticks.amount += other.ticks.amount;
}

void Time::operator-=(float other)
{
	ticks.amount -= (long long)other;
}

void Time::operator+=(float other)
{
	ticks.amount += (long long)other;
}

Time Time::operator+(const Time &other) const
{
	return Time({ ticks.amount + other.ticks.amount });
}

const Time Time::startTime = Time::milliseconds_since_epoch();