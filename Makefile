.PHONY: all clean

SANITIZE ?= -fsanitize=undefined,integer,nullability,memory
WARNINGS_IGNORE = -Wno-c++98-compat-extra-semi -Wno-old-style-cast -Wno-c++98-compat -Wno-zero-length-array -Wno-c++20-designator -Wno-invalid-offsetof -Wno-c++98-compat-pedantic -Wno-gnu-zero-variadic-macro-arguments

CXX = clang++
LDFLAGS ?= ${SANITIZE}
CXXFLAGS ?=  -MMD -g ${SANITIZE} -Wall -Wextra -pedantic -Weverything -Werror ${WARNINGS_IGNORE}

all: bin/yield bin/server bin/client bin/notification
clean:
	-rm bin/* example/*.o example/*.d src/*.o src/*.d  2>/dev/null

bin/yield: example/yield.o src/co.o
	${CXX} ${LDFLAGS} -o $@ $^
bin/server: example/server.o src/co.o
	${CXX} ${LDFLAGS} -o $@ $^
bin/client: example/client.o src/co.o
	${CXX} ${LDFLAGS} -o $@ $^
bin/notification: example/notification.o src/co.o
	${CXX} ${LDFLAGS} -o $@ $^

-include example/*.d
