#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <assert.h>
#include <math.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "prometheus-helper.h"

struct pmc_statm {
    size_t size;
    size_t resident;
    size_t shared;
    size_t text;
    size_t lib;
    size_t data;
    size_t dt;
};

typedef enum {
    RUNNING,
    SLEEPING,
    WAITING,
    ZOMBIE,
    STOPPED,
    TRACING_STOP,
    PAGING,
    DEAD,
    WAKEKILL,
    WAKING,
    PARKED
} pmc_pstate_e;

struct pmc_mapping {
    uintptr_t start;
    uintptr_t end;
    size_t size;

    uint8_t readable : 1;
    uint8_t writable : 1;
    uint8_t executable : 1;
    uint8_t shared : 1;
    uint8_t privat : 1;

    size_t offset;
    size_t device_major;
    size_t device_minor;
    size_t inode;
};

struct pmc_maps {
    size_t count;
    struct pmc_mapping *mappings;
};

struct pmc_stat {
    size_t pid;
    char process_name[512];
    pmc_pstate_e state;
    size_t parent_pid;
    size_t group_id;
    size_t session_id;
    size_t tty;
    size_t fg_group_id;
    size_t flags;
    size_t minflt;
    size_t cminflt;
    size_t majflt;
    size_t cmajflt;
    size_t utime;
    size_t stime;
    size_t cutime;
    size_t cstime;
    size_t priority;
    size_t nice;
    size_t num_threads;
    size_t itrealvalue;
    size_t starttime;
    size_t vsize;
    size_t rss;
    size_t rsslim;
    size_t startcode;
    size_t endcode;
    size_t startstack;
    size_t kstkesp;
    size_t kstkeip;
    size_t signal;
    size_t blocked;
    size_t sigignore;
    size_t sigcatch;
    size_t wchan;
    size_t nswap;
    size_t cnswap;
    size_t exit_signal;
    size_t processor;
    size_t rt_priority;
    size_t policy;
    size_t delayacct_blkio_ticks;
    size_t guest_time;
    size_t cguest_time;
    size_t start_data;
    size_t end_data;
    size_t start_brk;
    size_t arg_start;
    size_t arg_end;
    size_t env_start;
    size_t env_end;
    size_t exit_code;
};

struct pmc_meminfo
{
    size_t mem_total;
    size_t mem_free;
    size_t mem_available;
    size_t buffers;
    size_t cached;
    size_t swap_cached;
    size_t active;
    size_t inactive;
    size_t active_anon;
    size_t inactive_anon;
    size_t unevictable;
    size_t mlocked;
    size_t high_total;
    size_t high_free;
    size_t low_total;
    size_t low_free;
    size_t mmap_copy;
    size_t swap_total;
    size_t swap_free;
    size_t dirty;
    size_t writeback;
    size_t anon_pages;
    size_t mapped;
    size_t shmem;
    size_t slab;
    size_t s_reclaimable;
    size_t s_unreclaim;
    size_t kernel_stack;
    size_t page_tables;
    size_t quicklists;
    size_t nfs_unstables;
    size_t bounce;
    size_t writeback_tmp;
    size_t commit_limit;
    size_t committed_as;
    size_t v_malloc_total;
    size_t v_malloc_used;
    size_t v_malloc_chunk;
    size_t hardward_corrupted;
    size_t anon_huge_pages;
};

static pmc_pstate_e char_to_pstate(char c)
{
#define X(Enum, Char) \
    case Char: return Enum

    switch (c) {
        X(RUNNING, 'R');
        X(SLEEPING, 'S');
        X(WAITING, 'D');
        X(ZOMBIE, 'Z');
        X(STOPPED, 'T');
        X(TRACING_STOP, 't');
        X(PAGING, 'W');
        X(DEAD, 'X');
        X(DEAD, 'x');
        X(WAKEKILL, 'K');
        X(PARKED, 'P');
        default:
            break;
    }

    assert(0);
    /* unreachable */
    return RUNNING;
}

static int parse_one_mapping(FILE *f, struct pmc_mapping *out)
{
    char r, w, x, p;
    int res = fscanf(f, "%zx-%zx %c%c%c%c %zx %zx:%zx %zu",
        &out->start, &out->end, &r, &w, &x, &p, &out->offset,
        &out->device_major, &out->device_minor, &out->inode);
    fscanf(f, "%*[^\n]");
    fscanf(f, "\n");

    if (res != 10) {
        return 0;
    }

    out->size = out->end - out->start;
    out->readable = r == 'r';
    out->writable = w == 'w';
    out->executable = x == 'x';
    out->shared = p == 's';
    out->privat = p == 'p';

    return 1;
}

static void parse_maps(struct pmc_maps *out)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (f == NULL) { fprintf(stderr, "failed reading maps (1)\n"); return; };

    size_t capacity = 0;
    size_t usage = 0;

    out->mappings = NULL;

    do {
        struct pmc_mapping mapping;
        int res = parse_one_mapping(f, &mapping);
        if (res == 0) {
            break;
        }

        if (capacity <= usage) {
            capacity = capacity > 0 ? capacity * 2: 1;
            out->mappings = (struct pmc_mapping*)realloc(out->mappings,
                capacity * sizeof(*out->mappings));
        }

        out->mappings[usage] = mapping;
        usage++;

    } while (1);

    out->count = usage;
    out->mappings = (struct pmc_mapping*)realloc(out->mappings,
        usage * sizeof(*out->mappings));
    fclose(f);
}

static void free_maps(struct pmc_maps *maps)
{
    free(maps->mappings);
    memset(maps, 0, sizeof(*maps));
}

static void parse_stat(struct pmc_stat *dst)
{
    int res;
    char state = 0;
    size_t *it = NULL;

    memset(dst, 0, sizeof(*dst));
    FILE *f = fopen("/proc/self/stat", "r");
    if (f == NULL) { fprintf(stderr, "failed reading stat (1)\n"); return; };

    res = fscanf(f, "%zu %s %c",
        &dst->pid,
        dst->process_name,
        &state);
    if (res != 3) { fprintf(stderr, "failed reading stat (2)\n"); return; };
    dst->state = char_to_pstate(state);

    it = &dst->parent_pid;
    for (; (uintptr_t)it < (uintptr_t)(dst + 1); it++) {
        res = fscanf(f, " %zu", it);
        if (res != 1) { fprintf(stderr, "failed reading stat (3)\n"); return; };
    }

    fclose(f);
}

static void parse_meminfo(struct pmc_meminfo *out)
{
    int res;
    size_t *ptr = (size_t*)out;
    char suffix[3];
    FILE *f = NULL;
    size_t i;
    unsigned long tmp;

    memset(out, 0, sizeof(*out));
    f = fopen("/proc/meminfo", "r");
    if (f == NULL) { fprintf(stderr, "failed to open /proc/meminfo\n"); }

    for (i = 0; i < sizeof(*out) / sizeof(size_t); i++) {
        res = fscanf(f, "%*s %lu %2s\n", &tmp, suffix);
        ptr[i] = (size_t)tmp;

        if (res != 2) {
            fprintf(stderr, "failed to parse /proc/meminfo\n");
            memset(out, 0, sizeof(*out));
            break;
        }

        if (memcmp(suffix, "mB", 1) == 0) { ptr[i] *= (1 << 20); }
        else if (memcmp(suffix, "kB", 2) == 0) { ptr[i] *= (1 << 10); }
        else { fprintf(stderr, "/proc/meminfo, unknown suffix %s\n", suffix); }
    }

    fclose(f);
}

float pmc_get_vsize(void)
{
    struct pmc_stat info;
    parse_stat(&info);
    return info.vsize;
}

float pmc_get_anonymous_mappings_size(void)
{
    struct pmc_maps maps;
    size_t i;
    float size = 0.f;

    parse_maps(&maps);


    for (i = 0; i < maps.count; i++) {
        if (maps.mappings[i].inode != 0) {
            continue;
        }
        size += (float)maps.mappings[i].size;
    }

    free_maps(&maps);
    return size;
}

float pmc_get_available_memory(void)
{
    struct pmc_meminfo info;
    parse_meminfo(&info);
    return (float)info.mem_available;
}
