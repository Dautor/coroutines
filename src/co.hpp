#pragma once

#include <functional>

namespace co
{
	std::size_t const constexpr DefaultStackSize = 32lu * 1024lu;

	void execute(); // do NOT call this while in a coroutine

	// use this one anywhere
	void add(std::function<void()>, std::size_t StackSize = DefaultStackSize);
#define CO(f, ...) ::co::add([=]() { f; } __VA_OPT__(, __VA_ARGS__))

	// use these only in a coroutine
	void yield();
	void wait_read(int fd);
	void wait_write(int fd);
};
