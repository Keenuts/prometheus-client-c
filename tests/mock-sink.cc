#include <unordered_map>

#include "mock-sink.hh"
#include "prometheus-client.h"

static std::unordered_map<std::string, float> *metrics_store;

void init_mock()
{
    metrics_store = new std::unordered_map<std::string, float>;
}

void deinit_mock()
{
    delete metrics_store;
}

void pmc_output_data(const void *bytes, size_t size)
{
    write(1, bytes, size);
}
