CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common

OBJS = protocol.o main.o
TARGET = ground_station

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
