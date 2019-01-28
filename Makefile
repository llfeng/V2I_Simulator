
all: tag reader trace

tag: tag.cpp common.cpp is_available.cpp
	g++ tag.cpp common.cpp is_available.cpp lambertian.cpp -o tag -lpthread -std=c++11
reader: reader.cpp common.cpp is_available.cpp
	g++ reader.cpp common.cpp is_available.cpp lambertian.cpp -o reader -lpthread -std=c++11
trace: trace.cpp common.cpp
	g++ trace.cpp common.cpp -o trace



.PHONY:clean
clean:
	rm -rf reader tag trace

