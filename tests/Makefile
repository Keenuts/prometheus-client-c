CXX ?= clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../ \
           -Wno-c++98-compat-pedantic -Wno-old-style-cast -Wno-unused-variable \
		   -Wno-missing-variable-declarations -g -O0 -ftest-coverage -fprofile-arcs
CFLAGS += -I../
LDLIBS=

BASE_OBJ= \
    ../prometheus-client.o \
	mock-sink.o \
	main.o

TEST_OBJ= \
    test-gauge.o \
    test-histogram.o

pmc-tests: CFLAGS += -ftest-coverage -fprofile-arcs -g -O0
pmc-tests:  ${BASE_OBJ} $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(LDLIBS) $^

proper:
	$(RM) ${BASE_OBJ} ${TEST_OBJ} $(wildcard *.gcov *.gcno *.gcda)

clean: proper
	$(RM) pmc-tests
