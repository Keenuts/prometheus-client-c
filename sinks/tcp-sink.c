#define _GNU_SOURCE

#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "prometheus-client.h"

int pmc_output_data(const void *bytes, size_t size)
{
    const char *HOSTNAME = "127.0.0.1";
    const char *PORT = "9091";
    struct addrinfo hints, *info;
    int sock, res;
    char rbuffer[1024];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(HOSTNAME, PORT, &hints, &info);

    sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (sock < 0) {
        return -1;
    }

    res = connect(sock, info->ai_addr, info->ai_addrlen);
    if (res < 0) {
        return -1;
    }

    freeaddrinfo(info);

    res = send(sock, bytes, size, 0);
    if (res < 0) {
        return -1;
    }

    res = recv(sock, rbuffer, sizeof(rbuffer), 0);
    if (res < 0) {
        return -1;
    }

    close(sock);

    if (strncmp(rbuffer, "HTTP/1.0 202", 12) != 0) {
        fprintf(stderr, "pushgate answer:\n%s\n", rbuffer);
        pmc_disable();
        return -1;
    }

    return 0;
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
