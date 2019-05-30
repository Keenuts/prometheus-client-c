#define _GNU_SOURCE

#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "prometheus-client.h"

void pmc_output_data(const void *bytes, size_t size)
{
    const char *HOSTNAME = "127.0.0.1";
    const char *PORT = "9091";
    struct addrinfo hints, *info;
    int sock, res;
    char rbuffer[1024];

    memset(&hints, 0, sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(HOSTNAME, PORT, &hints, &info);

    sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    assert(sock >= 0);
    res = connect(sock, info->ai_addr, info->ai_addrlen);
    assert(res >= 0);
    freeaddrinfo(info);

    send(sock, bytes, size, 0);

    recv(sock, rbuffer, sizeof(rbuffer), 0);
    close(sock);

    if (strncmp(rbuffer, "HTTP/1.0 202", 12) != 0) {
        fprintf(stdout, "%s", (const char*)bytes);
        fprintf(stderr, "pushgate answer:\n%s\n", rbuffer);
        assert(0);
    }

}
