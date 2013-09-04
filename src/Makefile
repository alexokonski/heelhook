STD= -std=c99 -pedantic
WARN= -Wall -Wunused-parameter
OPT?= -O2
DEBUG= -g -ggdb
SYMBOL= -D_BSD_SOURCE
VPATH= test sha1 base64 servers

FINAL_CFLAGS = $(STD) $(WARN) $(OPT) $(DEBUG) $(SYMBOL) $(CFLAGS)
FINAL_LDFLAGS = $(LDFLAGS) -g -ggdb
TEST_LIBS = -pthread
HEELHOOK_OBJECTS = hhmemory.o event.o darray.o protocol.o sha1.o cencode.o util.o server.o error_code.o

.PHONY: all
all: heelhook echoserver test

.PHONY: debug
debug: 
	$(MAKE) OPT=-O0 SYMBOL="-D_BSD_SOURCE -DDEBUG"

.PHONY:test_debug
test_debug:
	$(MAKE) test OPT=-O0 SYMBOL="-D_BSD_SOURCE -DDEBUG"


heelhook: $(HEELHOOK_OBJECTS)

.PHONY: test
test: test_event test_darray test_protocol test_util
	@echo
	@(bash runtests.sh $^)

test_event: test_event.o $(HEELHOOK_OBJECTS) 
	$(CC) $(TEST_LIBS) -o $@ $^

test_darray: test_darray.o $(HEELHOOK_OBJECTS)
	$(CC) $(TEST_LIBS) -o $@ $^

test_protocol: test_protocol.o $(HEELHOOK_OBJECTS)
	$(CC) $(TEST_LIBS) -o $@ $^

test_util: test_util.o $(HEELHOOK_OBJECTS)
	$(CC) $(TEST_LIBS) -o $@ $^

echoserver: echoserver.o $(HEELHOOK_OBJECTS)
	$(CC) -o $@ $^

include Makefile.dep

%.o: %.c
	$(CC) $(FINAL_CFLAGS) -c $<

.PHONY: dep
dep:
	find . | grep '\.c$$' | xargs $(CC) -MM > Makefile.dep

clean:
	rm -rf *.o
	rm -f echoserver
	rm -f test_event
	rm -f test_darray
	rm -f test_protocol
	rm -f test_util

