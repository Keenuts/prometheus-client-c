CC = clang
CFLAGS = -Wall -Wextra -std=c89 -I$(CURDIR) -g

main: main.o prometheus-helper.o
	$(CC) $(LDFLAGS) -o $@ $^
