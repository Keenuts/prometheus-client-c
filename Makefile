CC = clang
CFLAGS = -Wall -Wextra -std=c89 -I$(CURDIR)

tests:
	$(MAKE) -C tests

clean:
	$(MAKE) -C tests clean
