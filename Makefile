
all: tag reader

tag: tag.c
	gcc tag.c -o tag -lpthread
reader: reader.c
	gcc reader.c -o reader -lpthread


.PHONY:clean
clean:
	rm -rf reader tag

