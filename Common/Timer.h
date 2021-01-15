#ifndef CUSTOM_TIME_H_DEFINED
#define CUSTOM_TIME_H_DEFINED

class Time 
{
public:

	[[nodiscard]]
	static Time now();

	[[nodiscard]]
	float asMilliseconds();
	[[nodiscard]]
	float asSeconds();

	[[nodiscard]]
	Time operator-(const Time &other) const;
	[[nodiscard]]
	Time operator+(const Time &other) const;

	void operator-=(const Time &other);
	void operator+=(const Time &other);

private:

	Time(float givenTicks) : ticks(givenTicks) {}
	
	float ticks;
};	

#endif