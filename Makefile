SOURCES = main.c io.c process.c kafka.c
HEADERS = io.h kafka.h process.h
CFLAGS = -Wall -g
LIBS = -lrdkafka

# macOS homebrew flags
ifeq ($(shell uname),Darwin)
    CFLAGS += -I/opt/homebrew/include -L/opt/homebrew/lib
endif

.PHONY: all clean
all: bin/nosdk

bin:
	mkdir bin

bin/nosdk: $(SOURCES) $(HEADERS) | bin
	cc -o $@ $(CFLAGS) $(SOURCES) $(LIBS)

clean:
	rm -rf bin
