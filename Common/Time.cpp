#include "Time.h"
#include "Time.h"
#include "Time.h"
#include <chrono>

Time::Time(nanoseconds originalAmount) : ticks(originalAmount)
{
}

//https://stackoverflow.com/questions/31255486/c-how-do-i-convert-a-stdchronotime-point-to-long-and-back
Time::nanoseconds Time::nanosecondsSinceEpoch()
{
	auto now = std::chrono::system_clock::now();
	auto nanosecondsNow = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
	auto epoch = nanosecondsNow.time_since_epoch();
	return { epoch.count() };
}

float Time::secondsSinceEpoch()
{
	return Time(nanosecondsSinceEpoch()).asSeconds();
}

Time Time::now()
{
	return Time({ nanosecondsSinceEpoch().amount }) - startTime;
}

long long Time::asWholeSeconds() const
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds(ticks.amount)).count();
}

long long Time::asWholeMilliseconds() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(ticks.amount)).count();
}

float Time::asSeconds() const
{
	return asWholeMilliseconds() / 1000.0f;
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

const Time Time::startTime = Time::nanosecondsSinceEpoch();