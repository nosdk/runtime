.PHONY: all
all: bin bin/nosdk

bin:
	mkdir bin

bin/nosdk: main.c
	cc -o bin/nosdk -I/opt/homebrew/include -L/opt/homebrew/lib main.c -lrdkafka
