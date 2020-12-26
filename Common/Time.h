#pragma once

class Time
{
public:
	struct milliseconds
	{
		long long amount = 0;
	};

	Time(milliseconds originalAmount = { 0 });

	static milliseconds milliseconds_since_epoch();

	static float seconds_since_epoch();

	static Time now();

	float asSeconds() const;

	milliseconds asMilliseconds() const;

	[[nodiscard]]
	Time operator-(const Time &other) const;
	[[nodiscard]]
	Time operator+(const Time &other) const;

	void operator-=(const Time &other);
	void operator+=(const Time &other);
	void operator-=(float other);
	void operator+=(float other);

	static const Time startTime;

private:

	milliseconds ticks = {};
};