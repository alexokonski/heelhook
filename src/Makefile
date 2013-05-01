STD= -std=c99 -pedantic
WARN= -Wall -Wunused-parameter
OPT?= -O2
DEBUG= -g -ggdb
SYMBOL= -D_BSD_SOURCE
VPATH= test

FINAL_CFLAGS = $(STD) $(WARN) $(OPT) $(DEBUG) $(SYMBOL) $(CFLAGS)
FINAL_LDFLAGS = $(LDFLAGS) -g -ggdb
TEST_LIBS = -pthread
HEELHOOK_OBJECTS = hhmemory.o event.o

.PHONY: all
all: heelhook 

heelhook: $(HEELHOOK_OBJECTS)
	@echo "nothing yet!"

runcmd = ./$(1)

.PHONY: test
test: test_event
	@echo
	@(bash runtests.sh $^)

test_event: test_event.o $(HEELHOOK_OBJECTS) 
	$(CC) $(TEST_LIBS) -o $@ $^

%.o: %.c
	$(CC) $(FINAL_CFLAGS) -c $<

include Makefile.dep

.PHONY: dep
dep:
	find . | grep '\.c$$' | xargs $(CC) -MM *.c > Makefile.dep

clean:
	rm -rf *.o
