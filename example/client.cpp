#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "../src/co.hpp"

typedef int fd;

static fd ServerFD;

static void
HandleReadFD(fd FD)
{
	char Buffer[1024];
	while(true)
	{
		co::wait_read(FD);
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
		if(FD == ServerFD)
		{
			printf("%.*s", (int)Length, Buffer);
		} else
		{
			write(ServerFD, Buffer, (size_t)Length);
		}
	}
}

int
main()
{
	addrinfo Hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	addrinfo *Address;
	int       Result = getaddrinfo("localhost", "10503", &Hints, &Address);
	if(Result)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(Result));
		exit(EX_NOHOST);
	}
	fd FD = socket(Address->ai_family, Address->ai_socktype, Address->ai_protocol);
	if(FD == -1)
	{
		perror("socket");
		exit(EX_OSERR);
	}
	Result = connect(FD, Address->ai_addr, Address->ai_addrlen);
	if(Result == -1)
	{
		perror("connect");
		exit(EX_OSERR);
	}
	ServerFD = FD;
	CO(HandleReadFD(ServerFD));
	CO(HandleReadFD(0));
	co::execute();
	return 0;
}
