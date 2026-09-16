CC = gcc
CXX = g++
CFLAGS = -std=c11 -Wall -Wextra -I../../common
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../common -I../central_computer

# lnc_link_client/management_command/log/data_collection are central_computer's
# own modules, genuinely reused here (not copied) for CentralComputer's LNC
# connectivity — see central_computer.h's note.
OBJS = protocol.o main.o submarine.o research_submarine.o combat_submarine.o fleet.o \
       central_computer.o lnc_link_client.o management_command.o log.o data_collection.o
TARGET = oop_fleet

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

protocol.o: ../../common/protocol.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.cpp submarine.h fleet.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

submarine.o: submarine.cpp submarine.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

research_submarine.o: research_submarine.cpp research_submarine.h submarine.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

combat_submarine.o: combat_submarine.cpp combat_submarine.h submarine.h central_computer.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

fleet.o: fleet.cpp fleet.h research_submarine.h combat_submarine.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

central_computer.o: central_computer.cpp central_computer.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

lnc_link_client.o: ../central_computer/lnc_link_client.cpp ../central_computer/lnc_link_client.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

management_command.o: ../central_computer/management_command.cpp ../central_computer/management_command.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

log.o: ../central_computer/log.cpp ../central_computer/log.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

data_collection.o: ../central_computer/data_collection.cpp ../central_computer/data_collection.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
