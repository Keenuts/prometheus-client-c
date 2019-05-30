#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "prometheus-client.h"

#if (defined(_MSC_VER) && !defined(__INTEL_COMPILER)) || defined(__MINGW32__)
    #define SIZE_T_FMT "%Iu"
#else
    #define SIZE_T_FMT "%zu"
#endif

#define ZERO_ALLOC(Type, Count) (Type*)calloc(sizeof(Type), Count)
#define ALLOC(Type, Count) (Type*)malloc(sizeof(Type) * Count)

pmc_metric_s* pmc_initialize(const char *jobname)
{
    pmc_metric_s *out = NULL;
    size_t len;

    out = ZERO_ALLOC(pmc_metric_s, 1);
    assert(NULL != out);

    len = strlen(jobname) + 1;
    out->jobname = ALLOC(char, len);
    assert(NULL != out->jobname);
    memcpy(out->jobname, jobname, len);

    return out;
}

void pmc_add_gauge(pmc_metric_s *m, const char* name, float value)
{
    struct pmc_item_gauge *item = NULL;
    size_t len;

    item = ZERO_ALLOC(struct pmc_item_gauge, 1);
    assert(NULL != item);


    len = strlen(name) + 1;
    item->name = ALLOC(char, len);
    assert(NULL != item->name);
    memcpy(item->name, name, len);

    item->value = value;

    item->list.next = m->head;
    item->list.type = PM_GAUGE;
    m->head = &item->list;
}

void pmc_add_histogram(pmc_metric_s *m,
                       const char *name,
                       size_t size,
                       const float *buckets,
                       const float *values)
{
    struct pmc_item_histogram *item = NULL;
    size_t len;

    item = ZERO_ALLOC(struct pmc_item_histogram, 1);
    assert(NULL != item);

    len = strlen(name) + 1;
    item->name = ALLOC(char, len);
    assert(NULL != item->name);
    memcpy(item->name, name, len);

    item->size = size;
    item->values = ALLOC(float, size);
    item->buckets = ALLOC(float, size);
    assert(NULL != item->name);
    assert(NULL != item->values);
    assert(NULL != item->buckets);

    memcpy(item->values, values, size * sizeof(float));
    memcpy(item->buckets, buckets, size * sizeof(float));

    item->list.next = m->head;
    item->list.type = PM_HISTOGRAM;
    m->head = &item->list;
}

void pmc_update_histogram(pmc_metric_s *m,
                          const char *name,
                          size_t size,
                          const float *values)
{
    struct pmc_item_list *it = NULL;
    struct pmc_item_histogram *item = NULL;

    it = m->head;

    while (NULL != it) {
        item = (struct pmc_item_histogram*)it;
        if (it->type != PM_HISTOGRAM || 0 != strcmp(name, item->name)) {
            continue;
        }

        memcpy(item->values, values, size * sizeof(float));
        return;
    }

    assert(0);
}

static void send_http_packet(const char *jobname, const char* body)
{
#define HOSTNAME "127.0.0.1"
#define HTTP_FMT "POST /metrics/job/%s HTTP/1.0\r\n"                   \
                 "Host: " HOSTNAME "\r\n"                              \
                 "Content-type: application/x-www-form-urlencoded\r\n" \
                 "Content-length: " SIZE_T_FMT "\r\n\r\n"
    size_t len;
    char *out = NULL;
    FILE *f = NULL;

    f = tmpfile();
    assert(NULL != f);

    len = strlen(body);
    fprintf(f, HTTP_FMT, jobname, len);
    fwrite(body, len + 1, 1, f);

    len = (size_t)ftell(f);
    rewind(f);

    out = ALLOC(char, len);
    fread(out, len, 1, f);
    fclose(f);

    pmc_output_data(out, len);
    free(out);
}

static void pmc_output_gauge(FILE *f, const char *jobname, struct pmc_item_gauge *it)
{
    fprintf(f, "# TYPE %s_%s gauge\n", jobname, it->name);
    fprintf(f, "%s_%s %f\n", jobname, it->name, (double)it->value);
}

static void pmc_output_histogram(FILE *f, const char *jobname, struct pmc_item_histogram *it)
{
    float sum = 0.f;
    float count = 0.f;
    size_t i;

    fprintf(f, "# TYPE %s_%s histogram\n", jobname, it->name);

    for (i = 0; i < it->size; i++) {
        count += it->values[i];
        fprintf(f, "%s_%s_bucket{le=\"%f\"} %f\n",
            jobname, it->name, (double)it->buckets[i], (double)count
        );
        sum += it->values[i] * it->buckets[i];
    }

    fprintf(f, "%s_%s_count %f\n", jobname, it->name, (double)count);
    fprintf(f, "%s_%s_sum %f\n", jobname, it->name, (double)sum);
}

void pmc_send(pmc_metric_s *metric)
{
    FILE *f = NULL;
    struct pmc_item_list *head = NULL;
    size_t body_size = 0;
    char *body = NULL;

    f = tmpfile();
    assert(NULL != f);

    head = metric->head;
    while (head != NULL) {
        switch (head->type) {
            case PM_GAUGE:
                pmc_output_gauge(f, metric->jobname, (struct pmc_item_gauge*)head);
                break;
            case PM_HISTOGRAM:
                pmc_output_histogram(f, metric->jobname, (struct pmc_item_histogram*)head);
                break;
            case PM_TYPE_COUNT: /* fallthrough */
            case PM_NONE:       /* fallthrough */
                assert(0);
                break;
        }
        head = head->next;
    }

    body_size = (size_t)ftell(f);
    rewind(f);

    body = ALLOC(char, body_size + 1);
    fread(body, body_size, 1, f);
    fclose(f);
    body[body_size] = 0;

    send_http_packet(metric->jobname, body);
    free(body);
}

void pmc_destroy(pmc_metric_s *metric)
{
    struct pmc_item_list *head = NULL;
    struct pmc_item_list *next = NULL;
    struct pmc_item_gauge *g = NULL;
    struct pmc_item_histogram *h = NULL;

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
            assert(0);
            break;
        }

        head = next;
    }

    free(metric->jobname);
    free(metric);
}


void pmc_send_gauge(const char* job_name, const char* name, float value)
{
    pmc_metric_s *m = pmc_initialize(job_name);
    pmc_add_gauge(m, name, value);
    pmc_send(m);
    pmc_destroy(m);
}

void pmc_send_histogram(const char* jobname,
                        const char* name,
                        size_t size,
                        const float *buckets,
                        const float *values)
{
    pmc_metric_s *m = pmc_initialize(jobname);
    pmc_add_histogram(m, name, size, buckets, values);
    pmc_send(m);
    pmc_destroy(m);
}
