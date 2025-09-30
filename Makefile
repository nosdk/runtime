SOURCES = io.c process.c kafka.c config.c http.c postgres.c util.c s3.c
HEADERS = io.h kafka.h process.h config.h http.h postgres.h util.h s3.h
CFLAGS = -Wall -g -fsanitize=address -O0 -fsanitize=undefined
LIBS = -lrdkafka -lcyaml -lpq -laws-c-common -laws-c-io -laws-c-auth -laws-c-http -laws-c-s3

# macOS homebrew flags
ifeq ($(shell uname),Darwin)
    CFLAGS += -I/opt/homebrew/include -L/opt/homebrew/lib -I/opt/homebrew/opt/libpq/include -L/opt/homebrew/opt/libpq/lib
    CFLAGS += -I/opt/homebrew/opt/aws-c-common/include -L/opt/homebrew/opt/aws-c-common/lib
    CFLAGS += -I/opt/homebrew/opt/aws-c-io/include -L/opt/homebrew/opt/aws-c-io/lib
    CFLAGS += -I/opt/homebrew/opt/aws-c-auth/include -L/opt/homebrew/opt/aws-c-auth/lib
    CFLAGS += -I/opt/homebrew/opt/aws-c-s3/include -L/opt/homebrew/opt/aws-c-s3/lib
    CFLAGS += -I/opt/homebrew/opt/aws-c-http/include -L/opt/homebrew/opt/aws-c-http/lib
endif

.PHONY: all test clean
all: bin/nosdk-run
test: bin/test_json
	./bin/test_json

bin:
	mkdir bin

bin/nosdk-run: $(SOURCES) $(HEADERS) main.c | bin
	cc -o $@ $(CFLAGS) $(SOURCES) main.c $(LIBS)


bin/test_json: $(SOURCES) $(HEADERS) test/test_json.c | bin
	cc -o $@ $(CFLAGS) $(SOURCES) test/test_json.c $(LIBS)

clean:
	rm -rf bin
