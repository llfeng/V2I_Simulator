ifeq ($(random), 1)
	CFLAG += -DRANDOM_INSERT_TAG=1
else
	CFLAG += -DRANDOM_INSERT_TAG=0
endif

ifeq ($(bps), 128)
	CFLAG += -DUPLINK_BITRATE=128
else
	CFLAG += -DUPLINK_BITRATE=256
endif

ifeq ($(signtype), 1)
	CFLAG += -DDEFAULT_SIGN_TYPE=1
else
	CFLAG += -DDEFAULT_SIGN_TYPE=2
endif


all: tag reader trace

tag: tag.cpp common.cpp is_available.cpp
	g++ $(CFLAG) tag.cpp common.cpp is_available.cpp lambertian.cpp -o tag -lpthread -std=c++11
reader: reader.cpp common.cpp is_available.cpp
	g++ $(CFLAG) reader.cpp common.cpp is_available.cpp lambertian.cpp -o reader -lpthread -std=c++11
trace: trace.cpp common.cpp
	g++ $(CFLAG) trace.cpp common.cpp -o trace



.PHONY:clean
clean:
	rm -rf reader tag trace

