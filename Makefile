# BuddyFS makefile
# Rick Carback, Emily Fetchko, Bryan Pass
# 621, spring '05

CXXFLAGS=-ansi -Wall -g3 -D_FILE_OFFSET_BITS=64 -D_REENTRANT -DFUSE_USE_VERSION=22 -I./

SOURCES=Socket.cpp Listener.cpp Packet.cpp Buddy.cpp Clique.cpp Request.cpp FileSystem.cpp drm.cpp
OBJS=Socket.o Listener.o Packet.o Buddy.o Clique.o Request.o FileSystem.o drm.o

all: make.dep BuddyFS
	
BuddyFS: ${OBJS}
	g++ -ansi -Wall -lpthread -lcrypto -o BuddyFS ${OBJS} libfuse.a
dep: 
	g++ ${CXXFLAGS} -MM *.cpp > make.dep
make.dep: 
	g++ ${CXXFLAGS} -MM *.cpp > make.dep
clean:
	rm -f *~ *.o core.* make.dep

-include make.dep
