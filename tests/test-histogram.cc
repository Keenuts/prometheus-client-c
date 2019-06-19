#include "test.hh"
#include "prometheus-client.h"
#include "mock-sink.hh"

CREATE_TEST(histogram, simple_send)
{
    const float buckets[5] = { 1.f, 5.f, 10.f, 20.f, 50.f };
    const float values[5] =  { 2.f, 4.f, 9.f, 19.f, 49.f };
    pmc_send_histogram("test_hist", "histogram_1", 5, buckets, values);

    assert_eq(mock_histogram_get_count(), 1UL);
    assert_eq(mock_histogram_count_buckets("test_hist_histogram_1"), 5UL);

    float total = 0.f;
    for (size_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++) {
        total += values[i];
        assert_eq(mock_histogram_get_bucket("test_hist_histogram_1",
                                            buckets[i]), total);
    }
}

CREATE_TEST(histogram, simple_manual)
{
    const float buckets[5] = { 1.f, 5.f, 10.f, 20.f, 50.f };
    const float values[5] =  { 2.f, 4.f, 9.f, 19.f, 49.f };
    const float values_2[5] =  { 16.f, 15.f, 14.f, 13.f, 12.f };
    float total = 0.f;

    pmc_metric_s m = nullptr;

    /* manual histogram handling */
    m = pmc_initialize("test_hist");
    pmc_add_histogram(m, "histogram_2", 5, buckets, values);
    pmc_send(m);

    assert_eq(mock_histogram_get_count(), 1UL);
    assert_eq(mock_histogram_count_buckets("test_hist_histogram_2"), 5UL);
    total = 0.f;
    for (size_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++) {
        total += values[i];
        assert_eq(mock_histogram_get_bucket("test_hist_histogram_2",
                                            buckets[i]), total);
    }

    pmc_update_histogram(m, "histogram_2", 5, values_2);
    pmc_send(m);

    assert_eq(mock_histogram_get_count(), 1UL);
    assert_eq(mock_histogram_count_buckets("test_hist_histogram_2"), 5UL);
    total = 0.f;
    for (size_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++) {
        total += values_2[i];
        assert_eq(mock_histogram_get_bucket("test_hist_histogram_2",
                                            buckets[i]), total);
    }

    pmc_destroy(m);
}
