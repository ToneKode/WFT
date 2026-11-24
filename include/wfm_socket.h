#ifndef WFM_SOCKET_H
#define WFM_SOCKET_H

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include "constants.h"
#include "types.h"
#include "wfm_json.h"
#include "ws_transport.h"

/**
 * @file wfm_socket.h
 * @brief WebSocket protocol layer for the realtime feed.
 *
 * The server speaks a small JSON envelope:
 *
 * @code
 * {"route": "...", "payload": {...}, "id": "...", "meta": {"stream": "...", "revision": 3}}
 * @endcode
 *
 * Commands answer with `<route>:ok` or `<route>:error` reusing the caller's
 * `id`; events arrive unprompted. This class owns envelope handling and event
 * dispatch and leaves framing to a WsTransport, so it is driven by canned
 * frames in tests without a socket.
 *
 * Only routes the server actually registers are supported. Item and profile
 * order subscriptions exist as backend stubs but are not public routes, so they
 * are deliberately absent rather than half-wired.
 *
 * ## Getting a transport
 *
 * This needs a libcurl compiled with WebSocket support. The vcpkg `curl` port
 * does not enable it, so supply your own WsTransport over a WS-capable client,
 * or poll through MarketAPI until one is available. `handle()` exists so
 * frames from an existing socket can be fed in directly.
 */
class WfmSocket
{
public:
    /// Invoked for each visible new-order event.
    using NewOrderHandler = std::function<void(const Order &)>;

    explicit WfmSocket(std::unique_ptr<WsTransport> transport)
        : transport_(std::move(transport))
    {
        if (!transport_)
        {
            throw std::invalid_argument("WfmSocket requires a transport");
        }
    }

    /// Connect, requesting the `wfm` subprotocol the server requires.
    void connect() { transport_->connect(WS_API, "wfm"); }

    void close() { transport_->close(); }

    bool isConnected() const { return transport_->isConnected(); }

    /**
     * @brief Subscribe to newly created visible orders.
     *
     * @param platform Optional platform filter; omit for all platforms.
     * @param crossplay Include crossplay orders. The server forces this false
     *        when `platform` is `switch`.
     * @return The command id, for correlation.
     *
     * Several subscriptions may be active at once (one per platform). The
     * filters are part of the subscription identity, so the same values must be
     * sent again to unsubscribe.
     */
    std::string subscribeNewOrders(const std::string &platform = "", bool crossplay = true)
    {
        return send("@wfm|cmd/subscribe/newOrders", filters(platform, crossplay));
    }

    /// Unsubscribe using the same filters given to `subscribeNewOrders`.
    std::string unsubscribeNewOrders(const std::string &platform = "", bool crossplay = true)
    {
        return send("@wfm|cmd/unsubscribe/newOrders", filters(platform, crossplay));
    }

    void onNewOrder(NewOrderHandler handler) { newOrderHandler_ = std::move(handler); }

    /**
     * @brief Read and dispatch up to one message.
     * @param timeout_ms How long to wait.
     * @return True when a message was handled.
     */
    bool poll(int timeout_ms = 1000)
    {
        std::string frame;
        if (!transport_->receive(frame, timeout_ms))
        {
            return false;
        }
        handle(frame);
        return true;
    }

    /**
     * @brief Feed a raw frame, bypassing the transport.
     *
     * For tests, and for callers that multiplex this feed onto their own
     * socket.
     */
    void handle(const std::string &frame)
    {
        nlohmann::json message;
        try
        {
            message = nlohmann::json::parse(frame);
        }
        catch (const nlohmann::json::parse_error &)
        {
            ++malformed_;
            return;
        }

        if (!message.is_object() || !wfm_json::hasKey(message, "route"))
        {
            ++malformed_;
            return;
        }

        const std::string route = message.at("route").get<std::string>();

        // Command replies: the route carries a :ok or :error suffix.
        if (endsWith(route, ":ok"))
        {
            ++acknowledged_;
            return;
        }
        if (endsWith(route, ":error"))
        {
            lastError_ = payloadText(message);
            return;
        }

        if (route == "@wfm|event/subscriptions/newOrder")
        {
            if (message.contains("payload") && message.at("payload").is_object())
            {
                const Order order = wfm_json::parseOrder(message.at("payload"));
                if (newOrderHandler_ && order.visible)
                {
                    newOrderHandler_(order);
                }
            }
            return;
        }

        if (route == "@wfm|protect/error")
        {
            lastError_ = payloadText(message);
        }

        // Unknown routes are ignored on purpose: the backend adds routes over
        // time and a strict client would start throwing.
    }

    /// Last command or protection error code, or empty when none seen.
    const std::string &lastError() const { return lastError_; }

    /// Frames that were not valid message envelopes.
    size_t malformedFrames() const { return malformed_; }

    /// Commands the server acknowledged.
    size_t acknowledgedCommands() const { return acknowledged_; }

private:
    std::unique_ptr<WsTransport> transport_;
    NewOrderHandler newOrderHandler_;
    std::string lastError_;
    size_t malformed_ = 0;
    size_t acknowledged_ = 0;
    unsigned nextId_ = 1;

    static bool endsWith(const std::string &value, const char *suffix)
    {
        const size_t length = std::char_traits<char>::length(suffix);
        return value.size() >= length &&
               value.compare(value.size() - length, length, suffix) == 0;
    }

    static std::string payloadText(const nlohmann::json &message)
    {
        return wfm_json::hasKey(message, "payload") && message.at("payload").is_string()
                   ? message.at("payload").get<std::string>()
                   : std::string("unknown");
    }

    static nlohmann::json filters(const std::string &platform, bool crossplay)
    {
        nlohmann::json payload = {{"crossplay", crossplay}};
        if (!platform.empty())
        {
            payload["platform"] = platform;
        }
        return payload;
    }

    std::string send(const char *route, const nlohmann::json &payload)
    {
        const std::string id = "wft-" + std::to_string(nextId_++);
        transport_->sendText(
            nlohmann::json{{"route", route}, {"id", id}, {"payload", payload}}.dump());
        return id;
    }
};

#endif // WFM_SOCKET_H
