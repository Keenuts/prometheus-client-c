CC ?= clang
CFLAGS = -Wall -Wextra -std=c89 -I$(CURDIR)

OBJ= \
	prometheus-client.o \
	metric-helpers/prometheus-helper.o

.PHONY: tests

all: ${OBJ} tests

tests:
	$(MAKE) -C tests

proper:
	$(RM) ${OBJ} $(wildcard *.gcov *.gcno *.gcda)
	$(MAKE) -C tests proper

clean: proper
	$(MAKE) -C tests clean
