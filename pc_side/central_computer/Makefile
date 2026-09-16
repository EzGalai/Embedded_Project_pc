CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common -pthread

OBJS = protocol.o main.o lnc_link_client.o log.o management_command.o comm_gs.o data_collection.o
TARGET = central_computer

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -pthread $(OBJS) -o $(TARGET)

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.cpp lnc_link_client.h management_command.h log.h comm_gs.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

lnc_link_client.o: lnc_link_client.cpp lnc_link_client.h log.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

log.o: log.cpp log.h lnc_link_client.h data_collection.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

management_command.o: management_command.cpp management_command.h lnc_link_client.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

comm_gs.o: comm_gs.cpp comm_gs.h data_collection.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

data_collection.o: data_collection.cpp data_collection.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
