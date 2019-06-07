#include <unistd.h>

#include "prometheus-client.h"

int pmc_output_data(const void *bytes, size_t size)
{
    return 0 <= write(1, bytes, size);
}

void pmc_handle_error(enum pmc_error err)
{
    switch (err) {
        case PMC_ERROR_ALLOCATION:
            fprintf(stderr, "pmc: an allocation failed. Disabling now.\n");
            pmc_disable();
            break;
        case PMC_ERROR_OUTPUT:
            fprintf(stderr, "pmc: output sink failed. Disabling now.\n");
            pmc_disable();
            break;

        case PMC_ERROR_COUNT: /* fallthrough */
        default:
            assert(0);
            break;
    };
}
