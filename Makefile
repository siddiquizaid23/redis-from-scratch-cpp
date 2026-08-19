CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -g -I./include
#                                          ↑ tells compiler to look in include/
LDFLAGS  = -lpthread

all: server client

server: src/server.cpp include/avl.h include/hashtable.h include/zset.h include/threads.h
	$(CXX) $(CXXFLAGS) src/server.cpp $(LDFLAGS) -o server

client: src/client.cpp
	$(CXX) $(CXXFLAGS) src/client.cpp -o client

clean:
	rm -f server client

.PHONY: all clean