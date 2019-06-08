#include "test.hh"
#include "mock-sink.hh"
#include "prometheus-client.h"

CREATE_TEST(gauge, simple_send)
{
    /* helper function for single gauge */
    pmc_send_gauge("test_gauge", "gauge", 0.5f);

    assert_eq(mock_get_gauge("test_gauge_gauge"), 0.5f);
}

CREATE_TEST(gauge, simple_manual)
{
    pmc_metric_s m = nullptr;

    /* classic method */
    m = pmc_initialize("test_gauge");

    pmc_add_gauge(m, "gauge_1", 0.6f);
    pmc_add_gauge(m, "gauge_2", 3.0f);
    pmc_send(m);
    pmc_destroy(m);

    assert_eq(mock_get_gauge_count(), 2UL);
    assert_eq(mock_get_gauge("test_gauge_gauge_1"), 0.6f);
    assert_eq(mock_get_gauge("test_gauge_gauge_2"), 3.0f);
}
