#include <stdio.h>

#include "../src/co.hpp"

static void
baz(char const *X)
{
	printf("\t\tbaz 0 %s\n", X);
	co::yield();
	printf("\t\tbaz 1 %s\n", X);
	co::yield();
	printf("\t\tbaz 2 %s\n", X);
}

static void
bar(char const *X)
{
	printf("\tbar 0 %s\n", X);
	co::yield();
	printf("\tbar 1 %s\n", X);
	CO(baz(X));
}

static void
foo(char const *X)
{
	printf("foo 0 %s\n", X);
	co::yield();
	printf("foo 1 %s\n", X);
	CO(bar(X));
}

int
main()
{
	CO(foo("A"));
	CO(foo("B"));
	CO(bar("C"));
	CO(baz("D"));
	co::execute();
	return 0;
}
