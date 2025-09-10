SOURCES = main.c io.c process.c kafka.c config.c http.c postgres.c
HEADERS = io.h kafka.h process.h config.h http.h postgres.h util.h
CFLAGS = -Wall -g -fsanitize=address -O0 -fsanitize=undefined
LIBS = -lrdkafka -lcyaml -lpq

# macOS homebrew flags
ifeq ($(shell uname),Darwin)
    CFLAGS += -I/opt/homebrew/include -L/opt/homebrew/lib -I/opt/homebrew/opt/libpq/include -L/opt/homebrew/opt/libpq/lib
endif

.PHONY: all clean
all: bin/nosdk

bin:
	mkdir bin

bin/nosdk: $(SOURCES) $(HEADERS) | bin
	cc -o $@ $(CFLAGS) $(SOURCES) $(LIBS)

clean:
	rm -rf bin
