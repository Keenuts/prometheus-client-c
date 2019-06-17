CC ?= clang
CFLAGS = -Wall -Wextra -std=c89 -I$(CURDIR)

OBJ= \
	prometheus-client.o \
	metric-helpers/prometheus-helper.o

all: ${OBJ} tests

tests:
	$(MAKE) -C tests

clean:
	$(RM) ${OBJ}
	$(MAKE) -C tests clean
