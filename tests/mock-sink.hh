#ifndef H_MOCK_SINK_
#define H_MOCK_SINK_

#include <string>

void mock_init(void);
void mock_deinit(void);

float mock_get_gauge(std::string name);
size_t mock_get_gauge_count();

#endif /* H_MOCK_SINK_ */
