STD= -std=c99 -pedantic
WARN= -Wall -Wunused-parameter
OPT?= -O2
DEBUG= -g -ggdb
SYMBOL= -D_BSD_SOURCE
VPATH= test sha1 base64

FINAL_CFLAGS = $(STD) $(WARN) $(OPT) $(DEBUG) $(SYMBOL) $(CFLAGS)
FINAL_LDFLAGS = $(LDFLAGS) -g -ggdb
TEST_LIBS = -pthread
HEELHOOK_OBJECTS = hhmemory.o event.o hhassert.o darray.o network.o sha1.o cencode.o 
#HEELHOOK_OBJECTS = sha1.o

.PHONY: all
all: heelhook 

.PHONY: debug
debug: 
	$(MAKE) OPT=-O0 SYMBOL="-D_BSD_SOURCE -DDEBUG"

.PHONY:test_debug
test_debug:
	$(MAKE) test OPT=-O0 SYMBOL="-D_BSD_SOURCE -DDEBUG"


heelhook: $(HEELHOOK_OBJECTS)
	@echo "nothing yet! $^"

.PHONY: test
test: test_event test_darray test_network
	@echo
	@(bash runtests.sh $^)

test_event: test_event.o $(HEELHOOK_OBJECTS) 
	$(CC) $(TEST_LIBS) -o $@ $^

test_darray: test_darray.o $(HEELHOOK_OBJECTS)
	$(CC) $(TEST_LIBS) -o $@ $^

test_network: test_network.o $(HEELHOOK_OBJECTS)
	$(CC) $(TEST_LIBS) -o $@ $^

#sha1.o: sha1/sha1.c sha1/sha1.h
#	$(CC) $(FINAL_CFLAGS) -c $<

include Makefile.dep

%.o: %.c
	$(CC) $(FINAL_CFLAGS) -c $<

.PHONY: dep
dep:
	find . | grep '\.c$$' | xargs $(CC) -MM > Makefile.dep

clean:
	rm -rf *.o
	rm -f test_event
	rm -f test_darray
	rm -f test_network

