# Simple C Prometheus client for unusual setups

I like Prometheus. For me, this tool is very convenient to get all kind
of metrics. I even use it to profile and visualize some behavior on my
applications.

There is several Prometheus clients available. They are well written, modern
and shiny.
Sadly, if you are in a pretty weird environment, it's will become complicated.

In the environments I used to work with, I had no libcurl, no wget, no bash,
weird tool-chains, weirder build-systems, and no internet.
Still I WANTED TO USE PROMETHEUS 🙃

Thus wrote this client. For now, it only contains push gateway options,
and only gauge and histograms. I do not need anything else for now.


The only need this code needs are:
- a std-C >= c89 compiler
