#ifndef H_MOCK_SINK_
#define H_MOCK_SINK_

#include <string>

void mock_init(void);
void mock_deinit(void);

float  mock_gauge_get_value(std::string name);
size_t mock_gauge_get_count();

float  mock_histogram_get_bucket(std::string name, float bucket);
size_t mock_histogram_count_buckets(std::string name);
size_t mock_histogram_get_count();

#endif /* H_MOCK_SINK_ */
