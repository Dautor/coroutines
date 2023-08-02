#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "../src/co.hpp"
#include "../src/dlist.hpp"

typedef int fd;

struct client
{
	size_t ID;
	fd     I;
	fd     O;
	dlist  List;
};

static dlist  Clients;
static size_t NextClientID;

static void Broadcast(size_t ID, char const *Message, size_t MessageLength);

static client *
ClientAlloc(fd I, fd O)
{
	auto C = (client *)calloc(1, sizeof(client));
	C->ID  = NextClientID++;
	C->I   = I;
	C->O   = O;
	insert_last(&Clients, &C->List);
	static char const *const Message       = "connected!\n";
	static size_t const      MessageLength = __builtin_strlen(Message);
	Broadcast(C->ID, Message, MessageLength);
	return C;
}

static void
Free(client *C)
{
	static char const *const Message       = "disconnected!\n";
	static size_t const      MessageLength = __builtin_strlen(Message);
	Broadcast(C->ID, Message, MessageLength);
	close(C->I);
	close(C->O);
	remove(&C->List);
	free(C);
}

static void
Broadcast(size_t ID, char const *Message, size_t MessageLength)
{
	char StringID[16];
	int  Length = snprintf(StringID, sizeof(StringID), "%zu: ", ID);
	if(Length < 0)
	{
		fprintf(stderr, "snprintf error in Broadcast\n");
		return;
	}
	dlist_for(I, &Clients, client, List)
	{
		// NOTE: it would be better if we used iovec here with a single syscall instead of two syscalls
		// ...but this is just an example application
		write(I->O, StringID, (size_t)Length);
		write(I->O, Message, MessageLength);
	}
}

static void
HandleClient(client *C)
{
	char Buffer[1024];
	while(true)
	{
		co::wait_read(C->I);
		ssize_t Length = read(C->I, Buffer, sizeof(Buffer));
		if(Length == -1)
		{
			fprintf(stderr, "[%zu] read: %s\n", C->ID, strerror(errno));
			Free(C);
			return;
		}
		if(Length == 0)
		{
			Free(C);
			return;
		}
		Broadcast(C->ID, Buffer, (size_t)Length);
	}
}

static void
HandleServer(fd FD)
{
	printf("Starting server [%d]!\n", FD);
	CO(HandleClient(ClientAlloc(STDIN_FILENO, STDOUT_FILENO)));
	while(true)
	{
		sockaddr_storage Address;
		bzero(&Address, sizeof(Address));
		socklen_t AddressLength = sizeof(Address);
		co::wait_read(FD);
		fd ClientFD = accept(FD, (sockaddr *)&Address, &AddressLength);
		if(ClientFD == -1)
		{
			perror("accept");
			return;
		}
		CO(HandleClient(ClientAlloc(ClientFD, ClientFD)));
	}
}

int
main()
{
	signal(SIGPIPE, SIG_IGN);
	addrinfo Hints = {
		.ai_flags    = AI_PASSIVE,
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
	{
		int Temp = 1;
		if(setsockopt(FD, SOL_SOCKET, SO_REUSEADDR, &Temp, sizeof(Temp)) == -1)
		{
			perror("setsockopt(SO_REUSEADDR)");
		}
	}
	if(bind(FD, Address->ai_addr, Address->ai_addrlen) == -1)
	{
		perror("bind");
		exit(EX_OSERR);
	}
	listen(FD, 1);
	init(&Clients);
	CO(HandleServer(FD));
	co::execute();
	return 0;
}
