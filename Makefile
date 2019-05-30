CC = clang
CFLAGS = -Wall -Wextra -std=c89 -I$(CURDIR)

SINK-NAME = tcp-sink
#SINK-NAME = term-sink

all: tests

tests: tests/test-gauge tests/test-histogram

tests/test-gauge: prometheus-client.o tests/test-gauge.o \
	sinks/${SINK-NAME}.o
tests/test-histogram: prometheus-client.o tests/test-histogram.o \
	sinks/${SINK-NAME}.o

clean:
	$(RM)  $(wildcard *.o)

clean:
	$(RM) $(wildcard *.o sinks/*.o tests/*.o)
	$(RM) tests/test-histogram tests/test-gauge
