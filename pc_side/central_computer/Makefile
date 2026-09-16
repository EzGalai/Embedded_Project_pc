CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common -pthread

OBJS = protocol.o main.o dca.o
TARGET = central_computer

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -pthread $(OBJS) -o $(TARGET)

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.cpp dca.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

dca.o: dca.cpp dca.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
