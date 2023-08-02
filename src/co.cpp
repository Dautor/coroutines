#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <ucontext.h>
#include <unistd.h>

#include "co.hpp"
#include "dlist.hpp"

#define ArrayCount(x) (sizeof(x) / sizeof(*(x)))
typedef int fd;

struct routine
{
	dlist                 List;
	ucontext_t            Context;
	std::function<void()> F;
	uint8_t               Stack[0];
};

struct engine
{
	fd         FD;
	uint8_t    Padding0[sizeof(void *) - sizeof(fd)];
	routine   *ExecutingAt;
	ucontext_t Context;
	dlist      Executing;
	dlist      Waiting;
};

static thread_local engine *E;

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

void
co::yield()
{
	if(swapcontext(&E->ExecutingAt->Context, &E->Context) == -1) assert(0);
}

static void
Free()
{
	dlist *Next;
	for(auto I = E->Executing.Next; I != &E->Executing; I = Next)
	{
		Next   = I->Next;
		auto _ = ContainerOf(I, routine, List);
		Free(_);
	}
	for(auto I = E->Waiting.Next; I != &E->Waiting; I = Next)
	{
		Next   = I->Next;
		auto _ = ContainerOf(I, routine, List);
		Free(_);
	}
	close(E->FD);
	free(E);
	E = nullptr;
}

static void
LambdaProc(routine *Co)
{
	Co->F();
	Free(Co);
}

static void
enable()
{
	E     = (engine *)calloc(1, sizeof(engine));
	E->FD = epoll_create1(0);
	init(&E->Executing);
	init(&E->Waiting);
	atexit(Free);
}

void
co::execute()
{
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
		} else if(empty(&E->Waiting) == false)
		{
			epoll_event Events[16];
			int         EventCount;
			while((EventCount = epoll_wait(E->FD, Events, ArrayCount(Events), -1)) == -1)
			{
				if(errno != EINTR) perror("epoll_wait");
			}
			for(int EventIndex = 0; EventIndex < EventCount; ++EventIndex)
			{
				auto Event = Events + EventIndex;
				auto Co    = (routine *)Event->data.ptr;
				remove(&Co->List);
				insert_first(&E->Executing, &Co->List);
			}
		} else
		{
			break;
		}
	}
}

void
co::add(std::function<void()> F, size_t StackSize)
{
	if(E == nullptr) enable();
	auto Co = RoutineAlloc(StackSize);
	Co->F   = F;
	if(getcontext(&Co->Context) == -1) assert(0);
	Co->Context.uc_stack.ss_sp   = Co->Stack;
	Co->Context.uc_stack.ss_size = StackSize;
	Co->Context.uc_link          = &E->Context;
	makecontext(&Co->Context, (void (*)())LambdaProc, 1, Co);
}

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

void
co::wait_read(fd FD)
{
	wait(FD, EPOLLIN);
}

void
co::wait_write(fd FD)
{
	wait(FD, EPOLLOUT);
}
