#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <ucontext.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/timerfd.h>

#include "co.hpp"
#include "dlist.hpp"

#if defined __FreeBSD__
#	include <sys/event.h>
#elif defined __linux__
#	include <sys/epoll.h>
#else
#	error unsupported OS
#endif

#define ArrayCount(x) (sizeof(x) / sizeof(*(x)))
typedef int fd;

#pragma clang diagnostic ignored "-Wthread-safety-negative"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct routine
{
	dlist                 List;
	ucontext_t            Context;
	co::notification     *Notification;
	std::function<void()> F;
	uint8_t               Stack[0];
};
#pragma clang diagnostic pop

struct engine
{
	dlist            List;
	engine         **Global;
	pthread_mutex_t *Mutex;
	fd               FD;
	uint8_t          Padding0[sizeof(void *) - sizeof(fd)];
	routine         *ExecutingAt;
	ucontext_t       Context;
	dlist            Executing;
	dlist            Waiting;
};

static thread_local engine *E;
static pthread_mutex_t      Mutex = PTHREAD_MUTEX_INITIALIZER;
static dlist                EngineList; // protected by Mutex

static routine *
RoutineAlloc(size_t StackSize)
{
	auto R = (routine *)calloc(1, sizeof(routine) + StackSize);
	insert_last(&E->Executing, &R->List);
	return R;
}

static void
Free(routine *R)
{
	remove(&R->List);
	free(R);
}

co::notification *
co::yield()
{
	if(swapcontext(&E->ExecutingAt->Context, &E->Context) == -1) assert(0);
	return E->ExecutingAt->Notification;
}

static void
Free(engine *Engine)
{
	pthread_mutex_lock(&Mutex);
	auto M = Engine->Mutex;
	pthread_mutex_lock(M);
	dlist *Next;
	for(auto I = Engine->Executing.Next; I != &Engine->Executing; I = Next)
	{
		Next = I->Next;
		Free(ContainerOf(I, routine, List));
	}
	for(auto I = Engine->Waiting.Next; I != &Engine->Waiting; I = Next)
	{
		Next = I->Next;
		Free(ContainerOf(I, routine, List));
	}
	close(Engine->FD);
	remove(&Engine->List);
	*Engine->Global = nullptr;
	free(Engine);
	pthread_mutex_unlock(M);
	free(M);
	pthread_mutex_unlock(&Mutex);
}

void
co::free()
{
	pthread_mutex_lock(&Mutex);
	dlist *Next;
	for(auto I = EngineList.Next; I != &EngineList; I = Next)
	{
		Next = I->Next;
		Free(ContainerOf(I, engine, List));
	}
	pthread_mutex_unlock(&Mutex);
}

static void
LambdaProc(routine *Co)
{
	Co->F();
	Free(Co);
	static thread_local ucontext_t _;
	swapcontext(&_, &E->Context);
}

static void
enable()
{
	pthread_mutex_lock(&Mutex);
	if(EngineList.Next == nullptr) init(&EngineList);
	E = (engine *)calloc(1, sizeof(engine));
#if defined __FreeBSD__
	E->FD = kqueue();
#elif __linux__
	E->FD = epoll_create1(0);
#else
#	error unsupported OS
#endif
	init(&E->Executing);
	init(&E->Waiting);
	E->Global = &E;
	E->Mutex  = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	*E->Mutex = PTHREAD_MUTEX_INITIALIZER;
	insert_first(&EngineList, &E->List);
	pthread_mutex_unlock(&Mutex);
}

void
co::execute()
{
	pthread_mutex_lock(E->Mutex);
	while(true)
	{
		if(empty(&E->Executing) == false)
		{
			dlist *Next;
			for(auto I = E->Executing.Next; I != &E->Executing; I = Next)
			{
				Next           = I->Next;
				auto Co        = ContainerOf(I, routine, List);
				E->ExecutingAt = Co;
				if(swapcontext(&E->Context, &Co->Context) == -1) assert(0);
			}
		}
		if(empty(&E->Waiting) == false)
		{
			bool OnlyWaiting = empty(&E->Executing);
#if defined __FreeBSD__
			struct kevent         Events[16];
			int                   EventCount;
			static timespec const T       = { .tv_sec = 0, .tv_nsec = 0 };
			auto                  Timeout = OnlyWaiting ? nullptr : &T;
			while((EventCount = kevent(E->FD, nullptr, 0, Events, ArrayCount(Events), Timeout)) == -1)
			{
				if(errno != EINTR)
				{
					perror("kevent");
					assert(0);
				}
			}
			for(int EventIndex = 0; EventIndex < EventCount; ++EventIndex)
			{
				auto Event = Events + EventIndex;
				auto Co    = (routine *)Event->udata;
				remove(&Co->List);
				insert_first(&E->Executing, &Co->List);
			}
#elif __linux__
			epoll_event Events[16];
			int         EventCount;
			while((EventCount = epoll_wait(E->FD, Events, ArrayCount(Events), OnlyWaiting ? -1 : 0)) == -1)
			{
				if(errno == EAGAIN) break;
				if(errno != EINTR)
				{
					perror("epoll_wait");
					assert(0);
				}
			}
			for(int EventIndex = 0; EventIndex < EventCount; ++EventIndex)
			{
				auto Event = Events + EventIndex;
				auto Co    = (routine *)Event->data.ptr;
				remove(&Co->List);
				insert_first(&E->Executing, &Co->List);
			}
#else
#	error unsupported OS
#endif
		} else
		{
			if(empty(&E->Executing)) break;
		}
	}
	pthread_mutex_unlock(E->Mutex);
}

void
co::add(std::function<void()> F, size_t StackSize)
{
	if(E == nullptr) enable();
	auto Co = RoutineAlloc(StackSize);
	Co->F   = std::move(F);
	if(getcontext(&Co->Context) == -1) assert(0);
	Co->Context.uc_stack.ss_sp   = Co->Stack;
	Co->Context.uc_stack.ss_size = StackSize;
	makecontext(&Co->Context, (void (*)())LambdaProc, 1, Co);
}

#if defined __FreeBSD__
static void
wait(fd FD, short Events)
{
	auto Co = E->ExecutingAt;
	assert(Co != nullptr);
	struct kevent Event;
	EV_SET(&Event, FD, Events, EV_ADD, 0, 0, Co);
	if(kevent(E->FD, &Event, 1, nullptr, 0, nullptr) == -1)
	{
		perror("kevent");
		assert(0);
	}
	remove(&Co->List);
	insert_last(&E->Waiting, &Co->List);
	if(swapcontext(&Co->Context, &E->Context) == -1) assert(0);
	EV_SET(&Event, FD, Events, EV_DELETE, 0, 0, Co);
	if(kevent(E->FD, &Event, 1, nullptr, 0, nullptr) == -1)
	{
		perror("kevent");
		assert(0);
	}
}
#elif defined __linux__
static void
wait(fd FD, uint32_t Events)
{
	assert(E->ExecutingAt);
	auto        Co = E->ExecutingAt;
	epoll_event Event;
	Event.events   = Events;
	Event.data.ptr = Co;
	epoll_ctl(E->FD, EPOLL_CTL_ADD, FD, &Event);
	remove(&Co->List);
	insert_last(&E->Waiting, &Co->List);
	if(swapcontext(&Co->Context, &E->Context) == -1) assert(0);
	epoll_ctl(E->FD, EPOLL_CTL_DEL, FD, &Event);
}
#else
#	error unsupported OS
#endif

co::notification *
co::wait_read(fd FD)
{
	if(E->ExecutingAt->Notification != nullptr) return E->ExecutingAt->Notification;
#if defined __FreeBSD__
	wait(FD, EVFILT_READ);
#elif defined __linux__
	wait(FD, EPOLLIN);
#else
#	error unsupported OS
#endif
	return E->ExecutingAt->Notification;
}

co::notification *
co::wait_write(fd FD)
{
	if(E->ExecutingAt->Notification != nullptr) return E->ExecutingAt->Notification;
#if defined __FreeBSD__
	wait(FD, EVFILT_WRITE);
#elif defined __linux__
	wait(FD, EPOLLOUT);
#else
#	error unsupported OS
#endif
	return E->ExecutingAt->Notification;
}

co::notification *
co::sleep(timespec const *T)
{
	if(E->ExecutingAt->Notification != nullptr) return E->ExecutingAt->Notification;
	fd FD = timerfd_create(CLOCK_MONOTONIC, 0);
	if(FD == -1)
	{
		perror("timerfd_create");
		exit(EX_OSERR);
	}
	itimerspec IT;
	IT.it_value    = *T;
	IT.it_interval = {};
	int Result     = timerfd_settime(FD, 0, &IT, nullptr);
	if(Result == -1)
	{
		perror("timerfd_settime");
		exit(EX_OSERR);
	}
	wait_read(FD);
	close(FD);
	return E->ExecutingAt->Notification;
}

void
co::notify_all(notification *N)
{
	for(auto I = E->Waiting.Next; I != &E->Waiting; I = I->Next)
	{
		auto Co          = ContainerOf(I, routine, List);
		Co->Notification = N;
	}
	dlist *Next;
	for(auto I = E->Waiting.Next; I != &E->Waiting; I = Next)
	{
		Next             = I->Next;
		auto Co          = ContainerOf(I, routine, List);
		Co->Notification = N;
		remove(&Co->List);
		insert_first(&E->Executing, &Co->List);
	}
}

void
co::clear()
{
	E->ExecutingAt->Notification = nullptr;
}
