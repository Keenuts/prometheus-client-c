#include "test.hh"
#include "prometheus-client.h"

CREATE_TEST(gauge, simple_send)
{
    /* helper function for single gauge */
    pmc_send_gauge("test_gauge", "gauge", 0.5f);
}

CREATE_TEST(gauge, simple_manual)
{
    pmc_metric_s m = NULL;

    /* classic method */
    m = pmc_initialize("test_gauge");

    pmc_add_gauge(m, "gauge_1", 0.6f);
    pmc_add_gauge(m, "gauge_2", 2.6f);
    pmc_send(m);
    pmc_destroy(m);
}


