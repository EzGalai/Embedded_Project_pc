CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

OBJS = main.o
TARGET = ground_station

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
