#include "Timer.h"
#include <glfw/glfw3.h>

Time Time::now()
{
	return (float)glfwGetTime();
}

float Time::asMilliseconds()
{
	return now().asSeconds() * 1000.0f;
}

float Time::asSeconds()
{
	return now().ticks;
}

Time Time::operator-(const Time &other) const
{
	return {ticks - other.ticks};
}

Time Time::operator+(const Time &other) const
{
	return { ticks + other.ticks };
}

void Time::operator-=(const Time &other)
{
	ticks -= other.ticks;
}

void Time::operator+=(const Time &other)
{
	ticks += other.ticks;
}