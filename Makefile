STARTER_DIR=/comp/111/assignments/fs
TEST_DIR=$(STARTER_DIR)/tests
CC=gcc
LD=$(CC)
CPPFLAGS=-g -std=gnu11 -Wpedantic -Wall -Wextra
CFLAGS=-I.
LDFLAGS=
LDLIBS=
PROGRAM=command_line
TEST=test

all: $(PROGRAM)

$(TEST).o: $(TEST_DIR)/$(TEST).o
	ln -s $(TEST_DIR)/$(TEST).o

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(PROGRAM): $(PROGRAM).o jumbo_file_system.o basic_file_system.o raw_disk.o
	$(LD) $(CPPFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $^

$(TEST): $(TEST).o jumbo_file_system.o basic_file_system.o raw_disk.o
	$(LD) $(CPPFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $^

.PHONY:
clean:
	rm -f *.o $(PROGRAM) $(TEST) DISK

.PHONY:
check: $(TEST)
	rm -f TEST_DISK
	./$(TEST) -f
	rm -f TEST_DISK
