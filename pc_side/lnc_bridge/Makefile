CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common

OBJS = protocol.o transport_serial.o transport_serial_backed.o main.o
TARGET = lnc_bridge

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

transport_serial.o: transport_serial.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

transport_serial_backed.o: transport_serial_backed.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
