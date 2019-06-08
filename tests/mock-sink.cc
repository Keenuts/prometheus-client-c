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

static std::unordered_map<std::string, std::pair<mtype_e, float>> *metrics_store;
static std::unordered_map<std::string, float> *gauges;

void mock_init()
{
    metrics_store = new std::unordered_map<std::string, std::pair<mtype_e, float>>;
    gauges = new std::unordered_map<std::string, float>;
}

void mock_deinit()
{
    delete metrics_store;
    delete gauges;
}

float mock_get_gauge(std::string name)
{
    return (*gauges)[name];
}

size_t mock_get_gauge_count()
{
    return gauges->size();
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

static bool parse_metrics(std::list<std::string>& body)
{
    const std::regex re_metric_type("# TYPE ([A-Za-z0-9_]+) (histogram|gauge)");
    //const std::regex re_metric_value("([A-Za-z0-9_]+)(\\{le=\"[0-9\\.]+\"\\})? +[0-9\\.]+");
    const std::regex re_metric_gauge("([A-Za-z0-9_]+) +([0-9.]+)$");
    std::smatch match;

    std::vector<std::pair<std::string, float>> untyped;
    std::unordered_map<std::string, mtype_e> types;

    /* step 1: get all types */
    for (auto it = body.begin(); it != body.end(); ) {
        std::string& line = *it;
        if (false == std::regex_match(line, match, re_metric_type)) {
            it++;
            continue;
        }

        ASSERT_TRUE(match.size() == 3);
        mtype_e type = string2mtype(match[2]);
        ASSERT_TRUE(type != MT_INVALID);
        types.insert(std::make_pair(match[1], type));
        it = body.erase(it);
    }

    /* step 2: parse the gauges */
    for (auto it = body.begin(); it != body.end(); ) {
        std::string& line = *it;
        if (false == std::regex_match(line, match, re_metric_gauge)) {
            it++;
            continue;
        }

        ASSERT_TRUE(match.size() == 3);
        if (types.count(match[1]) == 1) {
            (*gauges)[match[1].str()] = std::stof(match[2]);
        } else {
            untyped.emplace_back(std::make_pair(match[1].str(), std::stof(match[2])));
        }
        it = body.erase(it);
    }

    return true;
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
    ASSERT_TRUE(nullptr != tmp);


    while (nullptr != tmp && false == has_content_length) {
        std::string line(tmp);
        if (false == has_http_rq) {
            /* first line MUST be POST ... HTTP/1.0 */
            ASSERT_TRUE(std::regex_match(line, match, re_rq));
            ASSERT_TRUE(match.size() == 3);
            out_hdr->is_post = match[1].str() == "POST";
            out_hdr->metric_name = match[2].str();
            has_http_rq = true;
        }

        if (std::regex_match(line, match, re_content_length)) {
            ASSERT_TRUE(false == has_content_length);
            has_content_length = true;
            ASSERT_TRUE(match.size() == 2);
            out_hdr->content_length = std::stoull(match[1].str());
        }
        else if (std::regex_match(line, match, re_content_type)) {
            ASSERT_TRUE(false == has_content_type);
            has_content_type = true;
        }
        else if (std::regex_match(line, match, re_hostname)) {
            ASSERT_TRUE(false == has_hostname);
            has_hostname = true;
            ASSERT_TRUE(match.size() == 2);
            out_hdr->hostname = match[1].str();
        }

        tmp = strtok_r(nullptr, "\r\n", &state);
    };

    /* now body should begin, and should not be empty */
    ASSERT_TRUE(nullptr != tmp);
    while (nullptr != tmp) {
        content_length += strlen(tmp) + 1;
        out_body.emplace_back(std::string(tmp));
        tmp = strtok_r(nullptr, "\r\n", &state);
    }

    /* missing content length will fail later. Just did not wanted to
     * trigger error on the content_length is the field was missing */
    if (has_content_length) {
        ASSERT_TRUE(content_length == out_hdr->content_length);
    }

    return has_hostname && has_content_length && has_content_type;
}

void pmc_output_data(const void *bytes, size_t size)
{
    char *buffer = (char*)malloc(sizeof(char) * size + 1);
    std::list<std::string> body;
    memmove(buffer, bytes, size);
    buffer[size] = 0;

    struct http_hdr hdr;
    ASSERT_TRUE(parse_http(buffer, &hdr, body));

    ASSERT_TRUE(hdr.is_post);
    ASSERT_TRUE(body.size() > 0);

    ASSERT_TRUE(parse_metrics(body));

    free(buffer);
}
