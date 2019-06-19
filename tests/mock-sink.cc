#include <assert.h>
#include <regex>
#include <sstream>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <list>

#include "test.hh"
#include "mock-sink.hh"
#include "prometheus-client.h"

typedef enum {
    MT_INVALID,
    MT_HISTOGRAM,
    MT_GAUGE
} mtype_e;

struct Histogram
{
    float count_;
    float sum_;
    std::vector<float> buckets_;
    std::vector<float> values_;
};

static std::unordered_map<std::string, std::pair<mtype_e, float>> *metrics_store;
static std::unordered_map<std::string, float> *gauges;
static std::unordered_map<std::string, Histogram> *histograms;

void mock_init()
{
    metrics_store = new std::unordered_map<std::string, std::pair<mtype_e, float>>;
    gauges = new std::unordered_map<std::string, float>;
    histograms = new std::unordered_map<std::string, Histogram>;
}

void mock_deinit()
{
    delete metrics_store;
    delete gauges;
    delete histograms;
}

float mock_gauge_get_value(std::string name)
{
    return (*gauges)[name];
}

size_t mock_gauge_get_count()
{
    return gauges->size();
}

float mock_histogram_get_bucket(std::string name, float bucket)
{
    ASSERT_TRUE(histograms->count(name) == 1, "unknown histogram '%s'",
                name.c_str());
    Histogram const& h = (*histograms)[name];

    for (size_t i = 0; i < h.buckets_.size(); i++) {
        if (compare(h.buckets_[i], bucket) == 0) {
            return h.values_[i];
        }
    }

    return 0.f;
}

size_t mock_histogram_count_buckets(std::string name)
{
    ASSERT_TRUE(histograms->count(name) == 1, "unknown histogram '%s'",
                name.c_str());
    Histogram const& h = (*histograms)[name];

    return h.buckets_.size();
}

size_t mock_histogram_get_count()
{
    return histograms->size();
}

static mtype_e string2mtype(std::string s)
{
    if (s == "histogram") {
        return MT_HISTOGRAM;
    }
    if (s == "gauge") {
        return MT_GAUGE;
    }
    return MT_INVALID;
}

static bool parse_histogram(std::list<std::string>& body)
{
    std::regex re_bucket("([A-Za-z0-9_]+)_bucket\\{le=\"([0-9\\.]+)\"\\}\\s+([0-9\\.]+)");
    std::regex re_count("([A-Za-z0-9_]+)_count +([0-9.]+)$");
    std::regex re_sum("([A-Za-z0-9_]+)_sum +([0-9.]+)$");
    std::smatch match;

    bool has_bucket = false;
    bool has_count = false;
    bool has_sum = false;

    Histogram histogram;
    std::string name;

    while (body.size() > 0 && !(has_bucket && has_count && has_sum))
    {
        std::string line = body.front();
        body.pop_front();

        if (std::regex_search(line, match, re_bucket)) {
            has_bucket = true;
            ASSERT_TRUE(4 == match.size(), "incomplete histogram bucket");

            name = match[1];
            histogram.buckets_.push_back(std::stof(match[2]));
            histogram.values_.push_back(std::stof(match[3]));
        }
        else if (std::regex_match(line, match, re_count)) {
            has_count = true;
            ASSERT_TRUE(3 == match.size(), "invalid histogram count");
            histogram.count_ = std::stoul(match[2]);
        }
        else if (std::regex_match(line, match, re_sum)) {
            has_sum = true;
            ASSERT_TRUE(3 == match.size(), "invalid histogram size");
            histogram.sum_ = std::stoul(match[2]);
        }
        else {
            fprintf(stderr, "error at '%s': invalid histogram.\n", line.c_str());
            return false;
        }
    }

    ASSERT_TRUE(has_bucket && has_count && has_sum, "incomplete histogram");

    assert(histogram.buckets_.size() == histogram.values_.size());
    histograms->insert_or_assign(name, histogram);
    return true;
}

static bool parse_gauge(std::list<std::string>& body)
{
    const std::regex re_metric_gauge("([A-Za-z0-9_]+) +([0-9.]+)$");
    std::smatch match;
    std::string line = body.front();
    body.pop_front();

    bool res = std::regex_match(line, match, re_metric_gauge);
    if (false == res || 3 != match.size()) {
        fprintf(stderr, "error at '%s': invalid gauge.\n", line.c_str());
        return false;
    }

    (*gauges)[match[1].str()] = std::stof(match[2]);
    return true;
}

static bool parse_metrics(std::list<std::string>& body)
{
    const std::regex re_metric_type("# TYPE ([A-Za-z0-9_]+) (histogram|gauge)");
    std::smatch match;

    bool result = true;
    mtype_e type = MT_INVALID;

    while (body.size() > 0 && result) {
        std::string line = body.front();
        body.pop_front();

        if (false == std::regex_match(line, match, re_metric_type)) {
            fprintf(stderr, "error at '%s': expected type.\n", line.c_str());
            return false;
        }

        type = string2mtype(match[2]);

        switch (type) {
        case MT_HISTOGRAM:
            result = parse_histogram(body);
            break;
        case MT_GAUGE:
            result = parse_gauge(body);
            break;
        case MT_INVALID: /* fallthrough */
            fprintf(stderr, "error at '%s': invalid type.\n", line.c_str());
            return false;
        }
    }

    return result;
}

struct http_hdr {
    bool is_post;
    char padding[7];
    std::string metric_name;
    std::string hostname;
    size_t content_length;
};

static bool parse_http(char *input,
                       struct http_hdr *out_hdr,
                       std::list<std::string>& out_body)
{
    char *state = nullptr;
    size_t content_length = 0;
    bool has_http_rq = false;
    bool has_content_length = false;
    bool has_content_type = false;
    bool has_hostname = false;

    std::smatch match;
    const std::regex re_content_length("Content-length: *([0-9]+)");
    const std::regex re_content_type("Content-type: *(.+)");
    const std::regex re_hostname("Host: *([^ ]+)");
    const std::regex re_rq("(POST|GET) /metrics/job/([a-zA-Z0-9_]+) HTTP/1.0");

    char *tmp = strtok_r(input, "\r\n", &state);
    /* first line MUST be valid. POST ... HTTP/1.0 */
    ASSERT_TRUE(nullptr != tmp, "http request ill-formed");


    while (nullptr != tmp && false == has_content_length) {
        std::string line(tmp);
        if (false == has_http_rq) {
            /* first line MUST be POST ... HTTP/1.0 */
            ASSERT_TRUE(std::regex_match(line, match, re_rq),
                        "Invalid http request header");
            ASSERT_TRUE(match.size() == 3, "invalid http request header");
            out_hdr->is_post = match[1].str() == "POST";
            out_hdr->metric_name = match[2].str();
            has_http_rq = true;
        }

        if (std::regex_match(line, match, re_content_length)) {
            ASSERT_TRUE(false == has_content_length,
                        "too many content-length in HTTP request");
            has_content_length = true;
            ASSERT_TRUE(match.size() == 2, "ill-formet content-length field");
            out_hdr->content_length = std::stoull(match[1].str());
        }
        else if (std::regex_match(line, match, re_content_type)) {
            ASSERT_TRUE(false == has_content_type,
                        "too many content-type field in HTTP request");
            has_content_type = true;
        }
        else if (std::regex_match(line, match, re_hostname)) {
            ASSERT_TRUE(false == has_hostname,
                        "too many hostname field in HTTP request");
            has_hostname = true;
            ASSERT_TRUE(match.size() == 2, "ill-formed hostname field");
            out_hdr->hostname = match[1].str();
        }

        tmp = strtok_r(nullptr, "\r\n", &state);
    };

    /* now body should begin, and should not be empty */
    ASSERT_TRUE(nullptr != tmp, "empty http body is invalid here");
    while (nullptr != tmp) {
        content_length += strlen(tmp) + 1;
        out_body.emplace_back(std::string(tmp));
        tmp = strtok_r(nullptr, "\r\n", &state);
    }

    /* missing content length will fail later. Just did not wanted to
     * trigger error on the content_length is the field was missing */
    if (has_content_length) {
        ASSERT_TRUE(content_length == out_hdr->content_length,
                    "HTTP header content-length and content length mismatch");
    }

    return has_hostname && has_content_length && has_content_type;
}

int pmc_output_data(const void *bytes, size_t size)
{
    char *buffer = (char*)malloc(sizeof(char) * size + 1);
    std::list<std::string> body;
    memmove(buffer, bytes, size);
    buffer[size] = 0;

    struct http_hdr hdr;
    ASSERT_TRUE(parse_http(buffer, &hdr, body), "failed to parse HTTP header");
    ASSERT_TRUE(hdr.is_post, "HTTP header is not POST");
    ASSERT_TRUE(body.size() > 0, "HTTP body is empty");

    ASSERT_TRUE(parse_metrics(body), "cannot parse metrics");

    free(buffer);

    return 0;
}

void pmc_handle_error(enum pmc_error err)
{
    (void)err;
    assert_eq(0, 1);
}
