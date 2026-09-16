CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common

BRIDGE_OBJS = protocol.o transport_serial.o transport_serial_backed.o bridge_server.o main.o

.PHONY: all clean

all: lnc_bridge

lnc_bridge: $(BRIDGE_OBJS)
	$(CXX) $(BRIDGE_OBJS) -o lnc_bridge

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

transport_serial.o: transport_serial.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

transport_serial_backed.o: transport_serial_backed.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

bridge_server.o: bridge_server.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(BRIDGE_OBJS) lnc_bridge
