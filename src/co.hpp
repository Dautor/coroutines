#pragma once

#include <functional>
#include <time.h>

namespace co
{
	std::size_t constexpr const DefaultStackSize = 1024lu * 1024lu;

	void execute(); // do NOT call this while in a coroutine

	// use this one anywhere
	void add(std::function<void()>, std::size_t StackSize = DefaultStackSize);
#define CO(f, ...) ::co::add([=]() { f; } __VA_OPT__(, __VA_ARGS__))

	void free();

	// use these only in a coroutine
	typedef void notification;

	notification *yield();
	notification *wait_read(int fd);
	notification *wait_write(int fd);
	notification *sleep(timespec const *);

	void notify_all(notification *);
	void clear();
};
