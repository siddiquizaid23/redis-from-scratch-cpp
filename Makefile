CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -g
LDFLAGS  = -lpthread

all: server client

server: server.cpp avl.h hashtable.h zset.h threads.h
	$(CXX) $(CXXFLAGS) server.cpp $(LDFLAGS) -o server

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

clean:
	rm -f server client

.PHONY: all clean