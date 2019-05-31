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

/* there is two methods to use this client:
 *  - using helper functions
 *  - using manual API
 *
 *  helper functions are easy. One call and the metric is up.
 *  manual API allow metrics batching, histogram update and so on.
 */

/* BEGIN MANUAL API */

/* initialize a metric set. Usually the first call */
pmc_metric_s* pmc_initialize(const char *jobname);

/*
 * add a gauge to the metric set. Already existing gauge are not
 * checked. Thus adding two time the same gauge WILL generate two
 * metrics with the same name, both showing in the final HTTP request.
 *
 *  m: the metric set. Created using **pmc_initialize**
 *  name: the name of the metric. Valid characters: [A-Za-z0-9_] (not checked)
 *  value: the value of the metric.
 */
void pmc_add_gauge(pmc_metric_s *m, const char* name, float value);

/*
 * add an histogram to the metric set. Already existing histograms are not
 * checked. Thus adding two time the same histogram WILL generate two
 * histograms with the same name. Both will show in the final HTTP request.
 *
 *  m: the metric set. Created using **pmc_initialize**
 *  name: the name of the metric. Valid characters: [A-Za-z0-9_] (not checked)
 *  size: the number of buckets. Also the size of the two following arrays.
 *  buckets: array of floats. Each entry represents 1 bucket.
 *  values: the number of values in each bucket. (Not the sum of the previous)
 */
void pmc_add_histogram(pmc_metric_s *m,
                       const char *name,
                       size_t size,
                       const float *buckets,
                       const float *values);

/*
 * update a previously created histogram. WILL FAIL if no histogram with
 * the name *name* can be found.
 * The histogram size will not be changed. The parameter **size** MUST be
 * smaller or equal to the size given when creating the histogram.
 * If **size** is smaller than the previously given size, only the first
 * **size** values will be updated.
 *
 *  m: the metric set. Created using **pmc_initialize**
 *  name: the name of the metric. Valid characters: [A-Za-z0-9_] (not checked)
 *  size: the number of buckets. Also the size of the two following arrays.
 *  values: the number of values in each bucket. (Not the sum of the previous)
 */
void pmc_update_histogram(pmc_metric_s *m,
                          const char *name,
                          size_t size,
                          const float *values);

/*
 * send the HTTP request to the push gateway. The metric set is not invalidated
 * or modified when sent. Thus it can be updated then resent without additional
 * operations.
 *
 * metric : the metric to send, previously created with pmc_initialize
 */
void pmc_send(pmc_metric_s *metric);

/*
 * free a previously initialized metric set
 * metric : the metric to send, previously created with pmc_initialize
 */
void pmc_destroy(pmc_metric_s *metric);

/* BEGIN HELPER API */

/*
 * Sends an HTTP request containing only one metric.
 *
 * jobname: the name of the job that will show on Prometheus
 * name: the name of the metric that will show on Prometheus
 * value: the value of the metric.
 *
 * **jobname** and **name** characters must be [A-Za-z0-9_] (non checked)
 */
void pmc_send_gauge(const char* job_name, const char* name, float value);

/*
 * Sends an HTTP request containing only one histogram.
 *
 * jobname: the name of the job that will show on Prometheus
 * name: the name of the histogram that will show on Prometheus
 * size: the number of buckets. Also the size of the two following arrays.
 * buckets: array of floats. Each entry represents 1 bucket.
 * values: the number of values in each bucket. (Not the sum of the previous)
 *
 * **jobname** and **name** characters must be [A-Za-z0-9_] (non checked)
 */
void pmc_send_histogram(const char* jobname,
                        const char* name,
                        size_t size,
                        const float *buckets,
                        const float *values);

#endif /* H_PROMETHEUS_CLIENT_ */
