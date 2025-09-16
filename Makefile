SOURCES = io.c process.c kafka.c config.c http.c postgres.c util.c
HEADERS = io.h kafka.h process.h config.h http.h postgres.h util.h
CFLAGS = -Wall -g -fsanitize=address -O0 -fsanitize=undefined
LIBS = -lrdkafka -lcyaml -lpq

# macOS homebrew flags
ifeq ($(shell uname),Darwin)
    CFLAGS += -I/opt/homebrew/include -L/opt/homebrew/lib -I/opt/homebrew/opt/libpq/include -L/opt/homebrew/opt/libpq/lib
endif

.PHONY: all test clean
all: bin/nosdk
test: bin/test_json
	./bin/test_json

bin:
	mkdir bin

bin/nosdk: $(SOURCES) $(HEADERS) main.c | bin
	cc -o $@ $(CFLAGS) $(SOURCES) main.c $(LIBS)


bin/test_json: $(SOURCES) $(HEADERS) test/test_json.c | bin
	cc -o $@ $(CFLAGS) $(SOURCES) test/test_json.c $(LIBS)

clean:
	rm -rf bin
