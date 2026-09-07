CC = gcc
CFLAGS = -std=c11 -Wall -Wextra

.PHONY: all test clean

all: test_protocol test_transport

test_protocol: protocol.c test_protocol.c
	$(CC) $(CFLAGS) $^ -o $@

test_transport: transport_mock.c test_transport.c
	$(CC) $(CFLAGS) $^ -o $@

test: test_protocol test_transport
	./test_protocol
	./test_transport

clean:
	rm -f test_protocol test_transport
