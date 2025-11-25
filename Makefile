CXX = clang++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -pthread

all: pc

pc: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o pc

clean:
	rm -f pc
