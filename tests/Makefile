CXX = clang++
CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -Weverything -I../ \
           -Wno-c++98-compat-pedantic -Wno-old-style-cast -Wno-unused-variable \
		   -Wno-missing-variable-declarations
CFLAGS += -I../
LDLIBS=

BASE_OBJ= \
    ../prometheus-client.o \
	mock-sink.o \
	main.o

TEST_OBJ= \
    test-gauge.o \
    test-histogram.o

pmc-tests:  ${BASE_OBJ} $(TEST_OBJ)
	$(CXX) -o $@ $(LDLIBS) $^

clean:
	$(RM) ${BASE_OBJ} ${TEST_OBJ}
	$(RM) pmc-tests
