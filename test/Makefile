CXX=g++
IDIR=../src
LIBS=-lgtest -lpthread -ltcmalloc
BDIR = ./bin
CXXFLAGS=-Wall -std=c++17 -O3 -I$(IDIR) $(LIBS)

tests: test-bin
	./bin/$<

test-bin:
	$(CXX) All_test.cpp $(CXXFLAGS) -o $(BDIR)/$@

clean:
	rm -rf ./bin/*
