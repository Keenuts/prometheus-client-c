#ifndef H_PROMETHEUS_CLIENT_
#define H_PROMETHEUS_CLIENT_

#include <stdint.h>
#include <unistd.h>

void pmc_output_data(const void *bytes, size_t size);

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

typedef struct {
    char *jobname;
    struct pmc_item_list *head;
} pmc_metric_s;

pmc_metric_s* pmc_initialize(const char *jobname);
void pmc_add_gauge(pmc_metric_s *m, const char* name, float value);
void pmc_add_histogram(pmc_metric_s *m,
                       const char *name,
                       size_t size,
                       const float *buckets,
                       const float *values);
void pmc_update_histogram(pmc_metric_s *m,
                          const char *name,
                          size_t size,
                          const float *values);
void pmc_send(pmc_metric_s *metric);
void pmc_destroy(pmc_metric_s *metric);
void pmc_send_gauge(const char* job_name, const char* name, float value);
void pmc_send_histogram(const char* jobname,
                        const char* name,
                        size_t size,
                        const float *buckets,
                        const float *values);

#endif /* H_PROMETHEUS_CLIENT_ */
