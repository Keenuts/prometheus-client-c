#include <unistd.h>

#include "prometheus-client.h"

void pmc_output_data(const void *bytes, size_t size)
{
    write(1, bytes, size);
}
