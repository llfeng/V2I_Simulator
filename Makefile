
all: tag reader trace

tag: tag.c common.c
	gcc tag.c common.c -o tag -lpthread
reader: reader.c common.c
	gcc reader.c common.c -o reader -lpthread
trace: trace.c common.c
	gcc trace.c common.c -o trace



.PHONY:clean
clean:
	rm -rf reader tag trace

