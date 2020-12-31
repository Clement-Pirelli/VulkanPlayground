#pragma once

class Time
{
public:
	struct nanoseconds
	{
		long long amount = {};
	};

	Time(nanoseconds originalAmount = {0});

	static nanoseconds nanosecondsSinceEpoch();

	static float secondsSinceEpoch();

	static Time now();

	long long asWholeSeconds() const;
	long long asWholeMilliseconds() const;
	float asSeconds() const;

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

	nanoseconds ticks = { 0 };
};