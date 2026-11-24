#ifndef TESTS_FAKE_TRANSPORT_H
#define TESTS_FAKE_TRANSPORT_H

#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "http_transport.h"

/**
 * @file fake_transport.h
 * @brief Scripted HttpTransport for offline tests.
 *
 * Records every request it receives and answers from a FIFO of queued
 * responses, so a test reads as "given these replies, expect these calls".
 */
class FakeTransport : public HttpTransport
{
public:
    struct Request
    {
        std::string method;
        std::string url;
        nlohmann::json payload;
        Headers headers;
    };

    struct Reply
    {
        std::string body;
        Headers headers;
        long status = 200;
    };

    // Queue the reply for the next request.
    void queueReply(const std::string &body,
                    Headers headers = Headers(),
                    long status = 200)
    {
        replies_.push_back(Reply{body, std::move(headers), status});
    }

    void queueJson(const nlohmann::json &body,
                   Headers headers = Headers(),
                   long status = 200)
    {
        queueReply(body.dump(), std::move(headers), status);
    }

    const std::vector<Request> &requests() const { return requests_; }

    size_t pendingReplies() const { return replies_.size(); }

    std::string post(const std::string &url,
                     const nlohmann::json &payload,
                     const Headers &headers,
                     Headers &response_headers) override
    {
        return record("POST", url, payload, headers, response_headers).body;
    }

    std::string patch(const std::string &url,
                      const nlohmann::json &payload,
                      const Headers &headers,
                      Headers &response_headers) override
    {
        return record("PATCH", url, payload, headers, response_headers).body;
    }

    std::string Get(const std::string &url, const Headers &headers) override
    {
        Headers sink;
        return record("GET", url, nlohmann::json(), headers, sink).body;
    }

    long Delete(const std::string &url, const Headers &headers) override
    {
        Headers sink;
        return record("DELETE", url, nlohmann::json(), headers, sink).status;
    }

private:
    std::deque<Reply> replies_;
    std::vector<Request> requests_;

    Reply record(const std::string &method,
                 const std::string &url,
                 const nlohmann::json &payload,
                 const Headers &headers,
                 Headers &response_headers)
    {
        requests_.push_back(Request{method, url, payload, headers});

        if (replies_.empty())
        {
            throw std::runtime_error("FakeTransport: no queued reply for " +
                                     method + " " + url);
        }

        const Reply reply = replies_.front();
        replies_.pop_front();
        response_headers = reply.headers;
        return reply;
    }
};

#endif // TESTS_FAKE_TRANSPORT_H
