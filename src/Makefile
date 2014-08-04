STD= -std=c99 -pedantic -fpic
WARN= -Wall -Wextra -Werror
OPT?= -O2
DEBUG= -g -ggdb
SYMBOL= -D_BSD_SOURCE -D_POSIX_C_SOURCE=199309
VPATH= test sha1 base64 servers
EXT_SYMBOL=

FINAL_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(SYMBOL) $(EXT_SYMBOL) $(CFLAGS)
FINAL_LDFLAGS= $(LDFLAGS) -g -ggdb
TEST_LIBS= -pthread
ENDPOINT_OBJECTS= hhmemory.o darray.o protocol.o sha1.o cencode.o util.o error_code.o endpoint.o hhlog.o
HEELHOOK_OBJECTS= $(ENDPOINT_OBJECTS) event.o server.o pqueue.o client.o
TEST_CC= $(CC) $(TEST_LIBS) -o $@ $^
SHARED_SONAME=libheelhook.so.1
SHARED_REALNAME=libheelhook.so.1.0


.PHONY: all
all: heelhook echoserver chatserver test_client test heelhook_static heelhook_shared

.PHONY: debug
debug: OPT=-O0
debug: EXT_SYMBOL="-DDEBUG"
debug: all

heelhook: $(HEELHOOK_OBJECTS)

heelhook_static: heelhook libheelhook.a

libheelhook.a:
	ar rvcs libheelhook.a $(HEELHOOK_OBJECTS)

heelhook_shared: heelhook
	$(CC) -shared -Wl,-soname,$(SHARED_SONAME) -o $(SHARED_REALNAME) $(HEELHOOK_OBJECTS)

.PHONY: test
test: test_event test_darray test_protocol test_util test_pqueue
	@echo
	@(bash runtests.sh $^)

test_event: test_event.o event.o pqueue.o hhmemory.o util.o darray.o
	$(TEST_CC)

test_darray: test_darray.o darray.o hhmemory.o util.o
	$(TEST_CC)

test_protocol: test_protocol.o darray.o protocol.o error_code.o hhmemory.o util.o sha1.o cencode.o
	$(TEST_CC)

test_util: test_util.o util.o
	$(TEST_CC)

test_pqueue: test_pqueue.o pqueue.o darray.o hhmemory.o
	$(TEST_CC)

test_client: $(ENDPOINT_OBJECTS) client.o test_client.o event.o pqueue.o
	$(TEST_CC)

echoserver: echoserver.o $(HEELHOOK_OBJECTS)
	$(TEST_CC)

chatserver: chatserver.o cJSON.o $(HEELHOOK_OBJECTS)
	$(TEST_CC) -lm

include Makefile.dep

%.o: %.c
	$(CC) $(FINAL_CFLAGS) -c $<

.PHONY: dep
dep:
	find . | grep '\.c$$' | xargs $(CC) -MM > Makefile.dep

clean:
	rm -rf *.o
	rm -f echoserver
	rm -f chatserver
	rm -f test_event
	rm -f test_darray
	rm -f test_protocol
	rm -f test_util
	rm -f test_pqueue
	rm -f test_client
	rm -f $(SHARED_REALNAME)
	rm -f libheelhook.a

