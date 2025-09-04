.PHONY: all
all: bin bin/nosdk

bin:
	mkdir bin

bin/nosdk: main.c kafka.c process.c
	cc -o bin/nosdk -I/opt/homebrew/include -L/opt/homebrew/lib -Wall main.c -lrdkafka
