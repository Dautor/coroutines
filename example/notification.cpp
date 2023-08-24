#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/co.hpp"

typedef int fd;

static void
HandleSTDIN()
{
	fd FD = STDIN_FILENO;
	while(auto N = co::wait_read(FD))
	{
		printf("Got notification: %s\n", (char const *)N);
		co::clear();
	}
	char    Buffer[1024];
	ssize_t Length = read(FD, Buffer, sizeof(Buffer));
	if(Length == -1)
	{
		fprintf(stderr, "[%d] read: %s\n", FD, strerror(errno));
		close(FD);
		return;
	}
	if(Length == 0)
	{
		printf("[%d] done!\n", FD);
		close(FD);
		return;
	}
	printf("GOT: %.*s", (int)Length, Buffer);
	co::notify_all((co::notification *)"Shutting down");
}

static void
Alarm(char const *Message)
{
	while(true)
	{
		timespec T;
		T.tv_sec  = 1;
		T.tv_nsec = 0;
		if(auto N = co::sleep(&T))
		{
			printf("Alarm notification: %s\n", (char const *)N);
			return;
		}
		co::notify_all((co::notification *)Message);
		co::clear();
	}
}

int
main()
{
	CO(HandleSTDIN());
	CO(Alarm("Test"));
	co::execute();
	return 0;
}
