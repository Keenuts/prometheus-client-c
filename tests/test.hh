#ifndef H_TEST_
#define H_TEST_

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

#endif /* H_TEST_ */
