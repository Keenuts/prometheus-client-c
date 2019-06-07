#include <stdio.h>
#include <unistd.h>

#include "mock-sink.hh"
#include "test.hh"

int main()
{
    test_fptr* func = reinterpret_cast<test_fptr*>(&__start_test_fptrs);
    const char** names = reinterpret_cast<const char**>(&__start_test_names);
    size_t len = static_cast<size_t>(&__stop_test_fptrs - &__start_test_fptrs);

    for (size_t i = 0; i < len; i++) {
        init_mock();
        fprintf(stderr, "executing %s\n", names[i]);
        func[i]();
        deinit_mock();
    }

    return 0;
}
