CXX=g++
IDIR=-I/usr/local/include
LIBS=`pkg-config --cflags --libs protobuf` -laws-cpp-sdk-s3 -laws-cpp-sdk-core -ltcmalloc
# CFLAGS will be the options passed to the compiler.
CXXFLAGS=-Wall -O3 -std=c++17 $(IDIR) $(LIBS)

all:
	$(CXX) main.cpp ./proto/DNNAllDetailTrain.pb.cc $(CXXFLAGS) -o main 

#install:
#uninstall:
#test:
#clean:

# For compiling proto, remember we need to use clang, or else reinstall protobuf using g++ as compiler
# g++-9 proto.cpp ./proto/DNNAllDetailTrain.pb.cc -o pr -std=c++17 `pkg-config --cflags --libs protobuf`
#  g++-9 aws.cpp -o aws -I/usr/local/include/ -std=c++17 -lpthread -laws-cpp-sdk-s3
