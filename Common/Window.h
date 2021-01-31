#pragma once
#define GLFW_INCLUDE_NONE
#include <glfw/glfw3.h>
#include <vec.h>

enum CursorMode
{
	normal = GLFW_CURSOR_NORMAL,
	hidden = GLFW_CURSOR_HIDDEN,
	disabled = GLFW_CURSOR_DISABLED
};

class Window
{
public:

	ivec2 resolution() const;
	bool shouldClose() const;
	GLFWwindow *get() { return handle; };

	void setKeyCallback(GLFWkeyfun callback);
	void setCursorCallback(GLFWcursorposfun callback);
	void setUserData(void *data);
	void setCursorMode(CursorMode mode);

	Window(ivec2 startingResolution, const char *title, GLFWmonitor *monitor = nullptr);
	~Window();
	
private:

	GLFWwindow *handle{};
};
