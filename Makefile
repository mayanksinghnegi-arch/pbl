CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall

all: backend/foodbridge

backend/foodbridge: backend/main.cpp backend/dsa.h backend/models.h backend/managers.h
	$(CXX) $(CXXFLAGS) backend/main.cpp -o backend/foodbridge -lpthread

run: backend/foodbridge
	./backend/foodbridge

demo: backend/foodbridge
	./backend/foodbridge --demo

test:
	$(CXX) $(CXXFLAGS) backend/tests.cpp -o backend/tests && ./backend/tests

reset:
	rm -rf data

clean:
	rm -f backend/foodbridge backend/tests backend/foodbridge.exe

.PHONY: all run demo test reset clean
