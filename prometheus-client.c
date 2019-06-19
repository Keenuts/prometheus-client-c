#if !defined(_GNU_SOURCE)
    #define _GNU_SOURCE
#endif

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "prometheus-client.h"

/* %zu became supported in MSVC starting VS2015 */
#if (defined(_MSC_VER) && !defined(__INTEL_COMPILER)) || defined(__MINGW32__)
    #define SIZE_T_FMT "%Iu"
#else
    #define SIZE_T_FMT "%zu"
#endif

/* if this code is compiled with cpp, warnings will be throwns because
 * of the implicit casts */
#define ZERO_ALLOC(Type, Count) (Type*)calloc(sizeof(Type), Count)
#define ALLOC(Type, Count) (Type*)malloc(sizeof(Type) * Count)

#define RET_ON_FALSE(Cond, Err, ...) \
    if (!(Cond)) {                   \
        pmc_handle_error(Err);       \
        return __VA_ARGS__;          \
    }

/* note regarding strdup:
 * strdup is not posix. With c89, _GNU_SOURCE is needed. On MSVC,
 * it's available. on std >= c99, it's available.
 * For now, strdup is replaced with malloc + strlen
 */

typedef enum {
    PM_NONE,
    PM_GAUGE,
    PM_HISTOGRAM,
    PM_TYPE_COUNT
} pmc_type_e;

struct pmc_item_list {
    struct pmc_item_list *next;
    pmc_type_e type;
    char padding[4];
};

struct pmc_item_gauge {
    struct pmc_item_list list;
    char *name;
    float value;
    char padding[4];
};

struct pmc_item_histogram {
    struct pmc_item_list list;
    char *name;
    size_t size;
    float *buckets;
    float *values;
};

struct pmc_metric {
    char *jobname;
    struct pmc_item_list *head;
};


struct wbuffer {
    char *ptr;
    size_t usage;
    size_t size;
};

/* wbuffer (write-buffer) is used as a replacement for tmpfile+fprintf.
 * I had to change that to be able to use this tool on Android.
 * (Some untrusted applications cannot have storage permissions, and
 * tmpfile requires it.
 * wbuffer requires a valid mmap/mremap/munmap implementation.
 *
 * FIXME: I do not have a windows machine right now to implement the
 * variant for Windows. Might rework that bit later
 */
static const size_t PAGE_SIZE = 4096;
typedef struct wbuffer* wbuffer_t;

static int pmc_disabled = 0;
#define CHECK_KILLSWITCH(...) \
    if (0 != pmc_disabled) return __VA_ARGS__

static size_t align_page(size_t size)
{
    return (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

/* create a new wbuffer. This function MUST be called before using a wbuffer.
 * PARAMETERS:
 *   (none)
 * RETURN VALUE:
 *  NULL  -> wbuffer creation failed
 *  other -> wbuffer is ready to be used
 */
static wbuffer_t wbuffer_create(void)
{
    wbuffer_t buffer = ZERO_ALLOC(struct wbuffer, 1);
    if (NULL == buffer) {
        return NULL;
    }

    void *ptr = mmap(NULL, PAGE_SIZE, PROT_WRITE | PROT_READ,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (MAP_FAILED == ptr) {
        free(buffer);
        return NULL;
    }

    buffer->usage = 0;
    buffer->size = PAGE_SIZE - sizeof(*buffer);
    buffer->ptr = (char*)ptr;

    return buffer;
}

/* SHOULD NOT BE USED DIRECTLY.
 * You should not have to use this function outside of wbuffer_* functions.
 *
 * This function will expand the wbuffer to fit at least *min_expansion* bytes
 * FIXME: implement a variation for systems without mremap.
 *
 * PARAMETERS:
 *   buffer: a previously created wbuffer_t
 *   min_expansion: the minimum byte-size increment you need.
 *
 * RETURN VALUE:
 *  -1 -> expansion failed. wbuffer is still valid and unchanged.
 *   0 -> wbuffer size has been increased.
 */
static int wbuffer_expand(wbuffer_t buffer, size_t min_expansion)
{
    const size_t old_size = buffer->size + sizeof(*buffer);
    const size_t new_size = align_page(old_size + min_expansion);
    void *ptr = NULL;

    assert(NULL != buffer);

    ptr = mremap((void*)buffer->ptr, old_size, new_size, MREMAP_MAYMOVE);
    if (MAP_FAILED == ptr) {
        return -1;
    }

    buffer->size = new_size - sizeof(*buffer);
    buffer->ptr = (char*)ptr;
    return 0;
}

/*
 * Analog to fprintf, but with a wbuffer. Except I do NOT accept NULL as *fmt*
 * FIXME: check __va_copy/va_copy availability on other systems.
 *
 * PARAMETERS:
 *   buffer: a previously created wbuffer_t
 *   fmt: a non NULL, 0 terminated string. Forwarded to vsnprintf, so
 *        same rules applies.
 *   ...: variadic arguments forwarded to printf like function.
 *
 * RETURN VALUE:
 *   res > 0 -> same return value as vsnprintf.
 *   res < 0 -> en error occured, either in vsnprintf or while expanding wbuf.
 */
static int wbuffer_printf(wbuffer_t buffer, const char *fmt, ...)
{
    va_list args_count;
    va_list args_write;
    int res = 0;

    assert(NULL != buffer);
    assert(NULL != fmt);

    va_start(args_count, fmt);
    __va_copy(args_write, args_count);

    do {
        /* null byte IS counted here. We always write the null byte */
        res = vsnprintf(NULL, 0, fmt, args_count) + 1;
        if (res < 0) {
            break;
        }

        if (buffer->usage + (size_t)res >= buffer->size) {
            if (0 != wbuffer_expand(buffer, buffer->usage + (size_t)res)) {
                res = -1;
                break;
            }
        }

        res = vsnprintf(buffer->ptr + buffer->usage, res, fmt, args_write);
        if (res < 0) {
            break;
        }

        /* null byte NOT counted here. We write it, but overwrite it in
         * subsequent writes. */
        buffer->usage += (size_t)res;
    } while (0);

    va_end(args_count);
    va_end(args_write);
    return res;
}

/* write *size* bytes from *data* to the wbuffer. This function does NOT
 * append any 0 byte at the end.
 *
 * PARAMETERS:
 *   buffer: a previously created wbuffer_t
 *   data: a non-NULL pointer to the data to copy
 *   size: size of data in bytes.
 *
 * RETURN VALUE:
 *  -1 -> wbuffer expansion failed. Copy not done.
 *   0 -> copy went well.
 */
static int wbuffer_write(wbuffer_t buffer, const void *data, size_t size)
{
    assert(NULL != buffer);
    assert(NULL != data);

    if (buffer->usage + size >= buffer->size) {
        if (0 != wbuffer_expand(buffer, buffer->usage + size)) {
            return -1;
        }
    }

    memmove(buffer->ptr + buffer->usage, data, size);
    buffer->usage += size;
    return 0;
}

/* Get the current usage of the wbuffer. This returns the size of the
 * valid data stored, not the whole buffer size.
 *
 * PARAMETERS:
 *   buffer: a previously created wbuffer_t
 *
 * RETURN VALUE:
 *  wbuffer usage in bytes. wbuffer does not guarantee a NULL byte is present
 *  at the end of the valid data.
 */
static size_t wbuffer_get_length(wbuffer_t buffer)
{
    assert(NULL != buffer);
    return buffer->usage;
}

/* Get the raw pointer to the underlying data storage. Write/Read operations
 * to this pointer are allowed while in the range returned by
 * wbuffer_get_length.
 *
 * PARAMETERS:
 *   buffer: a previously created wbuffer_t
 *
 * RETURN VALUE:
 *  the pointer to the underlying storage.
 */
static void* wbuffer_get_ptr(wbuffer_t buffer)
{
    assert(NULL != buffer);
    return buffer->ptr;
}

/* destroy a previously created wbuf and all the underlying storages.
 * Calling wbuffer_destroy twice with the same buffer is UB.
 *
 * FIXME: implement a variant for systems without munmap
 *
 * RETURN:
 *  -1 -> wbuffer destruction failed. wbuffer remains unchanged.
 *   0 -> wbuffer has been destroyed.
 */
static int wbuffer_destroy(wbuffer_t buffer)
{
    assert(NULL != buffer);
    if (0 != munmap((void*)buffer->ptr, buffer->size)) {
        return -1;
    }

    free(buffer);
    return 0;
}

void pmc_disable(void)
{
    pmc_disabled = 1;
}

pmc_metric_s pmc_initialize(const char *jobname)
{
    pmc_metric_s out = NULL;
    size_t len;

    CHECK_KILLSWITCH(NULL);

    out = ZERO_ALLOC(struct pmc_metric, 1);
    RET_ON_FALSE(NULL != out, PMC_ERROR_ALLOCATION, NULL);

    len = strlen(jobname) + 1;
    out->jobname = ALLOC(char, len);
    if (NULL == out) {
        free(out);
        pmc_handle_error(PMC_ERROR_ALLOCATION);
        return NULL;
    }

    memcpy(out->jobname, jobname, len);

    return out;
}

int pmc_add_gauge(pmc_metric_s m, const char* name, float value)
{
    struct pmc_item_gauge *item = NULL;
    char *str = NULL;
    size_t len;

    CHECK_KILLSWITCH(0);

    len = strlen(name) + 1;
    item = ZERO_ALLOC(struct pmc_item_gauge, 1);
    str = ALLOC(char, len);

    if (NULL == item || NULL == str) {
        free(item);
        free(str);
        pmc_handle_error(PMC_ERROR_ALLOCATION);
        return -1;
    }

    memcpy(str, name, len);

    item->name = str;
    item->value = value;
    item->list.next = m->head;
    item->list.type = PM_GAUGE;
    m->head = &item->list;
    return 0;
}

int pmc_add_histogram(pmc_metric_s m,
                      const char *name,
                      size_t size,
                      const float *buckets,
                      const float *values)
{
    struct pmc_item_histogram *item = NULL;
    char *str = NULL;
    size_t len;

    CHECK_KILLSWITCH(0);

    len = strlen(name) + 1;
    item = ZERO_ALLOC(struct pmc_item_histogram, 1);
    str = ALLOC(char, len);

    if (NULL == item || NULL == str) {
        free(item);
        free(str);
        pmc_handle_error(PMC_ERROR_ALLOCATION);
        return -1;
    }

    memcpy(str, name, len);

    item->name = str;
    item->size = size;
    item->values = ALLOC(float, size);
    item->buckets = ALLOC(float, size);

    if (NULL == item->values || NULL == item->buckets) {
        free(item->values);
        free(item->buckets);
        free(item->name);
        free(item);
        pmc_handle_error(PMC_ERROR_ALLOCATION);
        return -1;
    }

    memcpy(item->values, values, size * sizeof(float));
    memcpy(item->buckets, buckets, size * sizeof(float));

    item->list.next = m->head;
    item->list.type = PM_HISTOGRAM;
    m->head = &item->list;
    return 0;
}

int pmc_update_histogram(pmc_metric_s m,
                         const char *name,
                         size_t size,
                         const float *values)
{
    struct pmc_item_list *it = NULL;
    struct pmc_item_histogram *item = NULL;

    CHECK_KILLSWITCH(0);

    it = m->head;

    while (NULL != it) {
        item = (struct pmc_item_histogram*)it;
        if (it->type != PM_HISTOGRAM || 0 != strcmp(name, item->name)) {
            continue;
        }

        memcpy(item->values, values, size * sizeof(float));
        return 0;
    }

    pmc_handle_error(PMC_ERROR_INVALID_KEY);
    return -1;
}

static int send_http_packet(const char *jobname, const char* body)
{
#define HOSTNAME "127.0.0.1"
#define HTTP_FMT "POST /metrics/job/%s HTTP/1.0\r\n"                   \
                 "Host: " HOSTNAME "\r\n"                              \
                 "Content-type: application/x-www-form-urlencoded\r\n" \
                 "Content-length: " SIZE_T_FMT "\r\n\r\n"
    size_t len;
    int res = -1;
    wbuffer_t buffer = NULL;

    buffer = wbuffer_create();
    if (NULL == buffer) {
        pmc_handle_error(PMC_ERROR_ALLOCATION);
        return -1;
    }

    do {
        len = strlen(body);
        if (0 > wbuffer_printf(buffer, HTTP_FMT, jobname, len)) {
            break;
        }

        if (0 > wbuffer_write(buffer, body, len + 1)) {
            break;
        }

        len = wbuffer_get_length(buffer);
        if (0 != pmc_output_data(wbuffer_get_ptr(buffer), len)) {
            break;
        }

        res = 0;
    } while (0);

    if (0 != res) {
        pmc_handle_error(PMC_ERROR_OUTPUT);
    }

    wbuffer_destroy(buffer);
    return res;
}

static int pmc_output_gauge(wbuffer_t buffer, const char *jobname, struct pmc_item_gauge *it)
{
    int res;

    res = wbuffer_printf(buffer, "# TYPE %s_%s gauge\n", jobname, it->name);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    res = wbuffer_printf(buffer, "%s_%s %f\n", jobname, it->name,
                         (double)it->value);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    return 0;
}

static int pmc_output_histogram(wbuffer_t buffer, const char *jobname, struct pmc_item_histogram *it)
{
    int res;
    float sum = 0.f;
    float count = 0.f;
    size_t i;

    res = wbuffer_printf(buffer, "# TYPE %s_%s histogram\n", jobname, it->name);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    for (i = 0; i < it->size; i++) {
        count += it->values[i];
        res = wbuffer_printf(buffer, "%s_%s_bucket{le=\"%f\"} %f\n",
                             jobname, it->name, (double)it->buckets[i],
                             (double)count);
        RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

        sum += it->values[i] * it->buckets[i];
    }

    res = wbuffer_printf(buffer, "%s_%s_count %f\n", jobname, it->name,
                         (double)count);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    res = wbuffer_printf(buffer, "%s_%s_sum %f\n", jobname, it->name,
                         (double)sum);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);
    return 0;
}

int pmc_send(pmc_metric_s metric)
{
    wbuffer_t buffer;
    struct pmc_item_list *head = NULL;
    const char ZERO = 0;
    int res;

    CHECK_KILLSWITCH(0);

    buffer = wbuffer_create();
    RET_ON_FALSE(NULL != buffer, PMC_ERROR_ALLOCATION, -1);

    head = metric->head;
    while (head != NULL) {
        switch (head->type) {
            case PM_GAUGE:
                res = pmc_output_gauge(buffer, metric->jobname,
                                       (struct pmc_item_gauge*)head);
                RET_ON_FALSE(0 >= res, PMC_ERROR_OUTPUT, -1);
                break;
            case PM_HISTOGRAM:
                res = pmc_output_histogram(buffer, metric->jobname,
                                           (struct pmc_item_histogram*)head);
                RET_ON_FALSE(0 >= res, PMC_ERROR_OUTPUT, -1);
                break;
            case PM_TYPE_COUNT: /* fallthrough */
            case PM_NONE:       /* fallthrough */
                assert(0); /* implementation safeguard */
                break;
        }
        head = head->next;
    }

    res = wbuffer_write(buffer, &ZERO, 1);
    RET_ON_FALSE(0 >= res, PMC_ERROR_OUTPUT, -1);
    res = send_http_packet(metric->jobname, wbuffer_get_ptr(buffer));
    RET_ON_FALSE(0 >= res, PMC_ERROR_OUTPUT, -1);

    res = wbuffer_destroy(buffer);
    RET_ON_FALSE(0 >= res, PMC_ERROR_ALLOCATION, -1);

    return 0;
}

void pmc_destroy(pmc_metric_s metric)
{
    struct pmc_item_list *head = NULL;
    struct pmc_item_list *next = NULL;
    struct pmc_item_gauge *g = NULL;
    struct pmc_item_histogram *h = NULL;

    CHECK_KILLSWITCH();

    if (NULL == metric) {
        return;
    }

    head = metric->head;
    while (head != NULL) {
        next = head->next;

        switch (head->type) {
        case PM_GAUGE:
            g = (struct pmc_item_gauge*)head;
            free(g->name);
            free(g);
            break;
        case PM_HISTOGRAM:
            h = (struct pmc_item_histogram*)head;
            free(h->name);
            free(h->buckets);
            free(h->values);
            free(h);
            break;
        case PM_TYPE_COUNT: /* fallthrough */
        case PM_NONE:       /* fallthrough */
            assert(0); /* implementation safeguard */
            break;
        }

        head = next;
    }

    free(metric->jobname);
    free(metric);
}


int pmc_send_gauge(const char* job_name, const char* name, float value)
{
    int res;
    pmc_metric_s m = NULL;

    CHECK_KILLSWITCH(0);

    m = pmc_initialize(job_name);
    RET_ON_FALSE(NULL != m, PMC_ERROR_ALLOCATION, -1);

    res = pmc_add_gauge(m, name, value);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    res = pmc_send(m);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    pmc_destroy(m);
    return 0;
}

int pmc_send_histogram(const char* jobname,
                       const char* name,
                       size_t size,
                       const float *buckets,
                       const float *values)
{
    int res;
    pmc_metric_s m = NULL;

    CHECK_KILLSWITCH(0);

    m = pmc_initialize(jobname);
    RET_ON_FALSE(NULL != m, PMC_ERROR_ALLOCATION, -1);

    res = pmc_add_histogram(m, name, size, buckets, values);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    res = pmc_send(m);
    RET_ON_FALSE(res >= 0, PMC_ERROR_OUTPUT, -1);

    pmc_destroy(m);
    return 0;
}
