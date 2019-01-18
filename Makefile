
all: tag reader trace

tag: tag.cpp common.cpp
	g++ tag.cpp common.cpp -o tag -lpthread
reader: reader.cpp common.cpp
	g++ reader.cpp common.cpp -o reader -lpthread
trace: trace.cpp common.cpp
	g++ trace.cpp common.cpp -o trace



.PHONY:clean
clean:
	rm -rf reader tag trace

