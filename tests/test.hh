#ifndef H_TEST_
#define H_TEST_

#include <stdio.h>
#include <stdlib.h>
#include <type_traits>
#include <limits>

typedef void(*test_fptr)(void);

#define CREATE_FUNCTION(Name)                       \
    static void Name(void);                         \
    __attribute((__section__("test_fptrs")))        \
    test_fptr Name ## _fptr = &Name;                \
    __attribute((__section__("test_names")))        \
    const char* Name ## _name = #Name;              \
    void Name(void)

#define CREATE_TEST(Group, Name) \
    CREATE_FUNCTION(Group ## _ ## Name)

extern test_fptr __start_test_fptrs;
extern test_fptr __stop_test_fptrs;
extern const char* __start_test_names;
extern const char* __stop_test_names;

template<typename T>
static inline bool compare(T a, T b)
{
    /*
     * yes this IS ugly. But here we do not want an exact perfect check. We
     * just want to know if we outputed something close. Printf puts 6
     * decimals -> I compare 6.
     */
    return !(a < b + 0.00001f && a > b - 0.000001f);

}

template<typename T>
inline void assert_eq(T a, T b)
{
    if (a != b) {
        if constexpr (std::is_same<T, size_t>::value) {
            fprintf(stderr, "assert_eq: %zu != %zu.\n", a, b);
        }
        else {
            fprintf(stderr, "equality failed\n");
        }
        abort();
    }
}

template<>
inline void assert_eq(float a, float b)
{
    if (compare(a, b) == true) {
        fprintf(stderr, "assert_eq: %f != %f.\n", (double)a, (double)b);
        abort();
    }
}

#define ASSERT_TRUE(Cond)                                           \
    if (!(Cond)) {                                                  \
        fprintf(stderr, "condition failed: " #Cond "\n");           \
        abort();                                                    \
    }

#endif /* H_TEST_ */
