# Simple C Prometheus client for unusual setups


## Why another incomplete Prometheus client ?

I like Prometheus. For me, this tool is very convenient to get all kind
of metrics. I even use it to profile and visualize some behavior on my
applications.

There is several [Prometheus clients available](
https://prometheus.io/docs/instrumenting/clientlibs/).
They are well written, modern and shiny.
Sadly, if you are in a pretty weird environment, Using them might become
complicated.

The environment I am referring to had no libcurl, no wget, no bash,
weird tool-chains, weirder build-systems, and the device had no internet.
Still **I WANTED TO USE PROMETHEUS** 🙃
For now, it can only push histograms and gauges to a push gateway. (I do not
need anything else for now)

## Requirements

- a C/C++ compiler supporting at least c89 ?
- a sink implementation
- a basic posix-ish compliant libc

## What is a sink ?

Depending of the board and constraints I have, I need to send the data using
one way, or another.
To simplify the design, I only require one function.

```c
    void pmc_output_data(const void *bytes, size_t size);
```

I will use this function to send the HTTP request.


## Examples

Here are the files you need to look at for examples:

- tests/test-histogram.c
- tests/test-gauge.c
- sinks/tcp-sink.c

```c
    pmc_send_gauge("test_gauge", "gauge", 0.5f);
```

```c
    const float buckets[5] = { 1.f, 5.f, 10.f, 20.f, 50.f };
    const float values[5] =  { 2.f, 4.f, 9.f, 19.f, 49.f };

    pmc_send_histogram("test_hist", "histogram_1", 5, buckets, values);
```

```c
    pmc_metric_s *m = pmc_initialize("test_hist");
    pmc_add_histogram(m, "histogram_2", 5, buckets, values);
    pmc_send(m);

    ...

    pmc_update_histogram(m, "histogram_2", 5, values);
    pmc_send(m);

    ...

    pmc_destroy(m);
```
