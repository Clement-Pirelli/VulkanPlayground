#include "Window.h"
#include <assert.h>

ivec2 Window::resolution() const
{
    ivec2 result;
    glfwGetWindowSize(handle, &result.x(), &result.y());
    return result;
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(handle);
}

void Window::setKeyCallback(GLFWkeyfun callback)
{
    glfwSetKeyCallback(handle, callback);
}

void Window::setCursorCallback(GLFWcursorposfun callback)
{
    glfwSetCursorPosCallback(handle, callback);
}

void Window::setUserData(void *data)
{
    glfwSetWindowUserPointer(handle, data);
}

void Window::setCursorMode(CursorMode mode)
{
    glfwSetInputMode(handle, GLFW_CURSOR, static_cast<int>(mode));
}

Window::Window(ivec2 startingResolution, const char * title, GLFWmonitor* monitor)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    handle = glfwCreateWindow(startingResolution.x(), startingResolution.y(), title, monitor, NULL);
    assert(handle != NULL);
}

Window::~Window()
{
    glfwDestroyWindow(handle);
}
