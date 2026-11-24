#ifndef TESTS_FAKE_WS_TRANSPORT_H
#define TESTS_FAKE_WS_TRANSPORT_H

#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include "ws_transport.h"

/**
 * @file fake_ws_transport.h
 * @brief Scripted WsTransport for offline socket tests.
 *
 * Records sent frames and replays queued incoming frames, so protocol tests
 * read as "given these frames, expect these callbacks".
 */
class FakeWsTransport : public WsTransport
{
public:
    void connect(const std::string &url, const std::string &subprotocol) override
    {
        url_ = url;
        subprotocol_ = subprotocol;
        connected_ = true;
        ++connectCount_;
    }

    void close() override { connected_ = false; }

    bool isConnected() const override { return connected_; }

    void sendText(const std::string &message) override
    {
        if (!connected_)
        {
            throw std::runtime_error("FakeWsTransport: send on closed connection");
        }
        sent_.push_back(message);
    }

    bool receive(std::string &out, int) override
    {
        if (incoming_.empty())
        {
            return false;
        }
        out = incoming_.front();
        incoming_.pop_front();
        return true;
    }

    /// Queue a frame to be returned by receive().
    void queueFrame(const std::string &frame) { incoming_.push_back(frame); }

    const std::vector<std::string> &sent() const { return sent_; }
    const std::string &url() const { return url_; }
    const std::string &subprotocol() const { return subprotocol_; }
    int connectCount() const { return connectCount_; }

private:
    std::deque<std::string> incoming_;
    std::vector<std::string> sent_;
    std::string url_;
    std::string subprotocol_;
    bool connected_ = false;
    int connectCount_ = 0;
};

#endif // TESTS_FAKE_WS_TRANSPORT_H
