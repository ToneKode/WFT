#ifndef WFM_SOCKET_H
#define WFM_SOCKET_H

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
#include "constants.h"
#include "types.h"
#include "wfm_json.h"
#include "ws_client.h"
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
 * Only routes the server actually registers are supported. The new-order
 * stream is live; item and profile order subscriptions exist as backend stubs
 * and are not public routes, so calling them throws instead of sending.
 *
 * ## Getting a transport
 *
 * Hand it a `WsClient` for the library's own libcurl transport, or any other
 * `WsTransport`. A dead connection throws from `poll()` so the caller can
 * reconnect and resend the same `subscribeNewOrders` filters.
 *
 * `handle()` remains the seam for multiplexed sockets. If your client already
 * owns the socket — because it multiplexes other feeds onto it — pass a
 * `WsTransport` whose `sendText()` writes to your client and whose
 * `receive()` never returns a frame, then call `handle(frame)` for every text
 * frame you receive. Your client does the reading; `poll()` is only a
 * convenience for the case where a `WsTransport` owns the read loop.
 *
 * @code
 * WfmSocket socket(std::make_unique<WsClient>());
 * socket.onNewOrder([](const Order &order) { ... });
 * socket.connect();
 * socket.subscribeNewOrders("pc", true);
 * while (running) { socket.poll(1000); }
 * @endcode
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
    void connect() { transport_->connect(std::string(WS_API), "wfm"); }

    void close() { transport_->close(); }

    [[nodiscard]] bool isConnected() const { return transport_->isConnected(); }

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
    std::string subscribeNewOrders(std::string_view platform = "", bool crossplay = true)
    {
        return send("@wfm|cmd/subscribe/newOrders", filters(platform, crossplay));
    }

    /// Unsubscribe using the same filters given to `subscribeNewOrders`.
    std::string unsubscribeNewOrders(std::string_view platform = "", bool crossplay = true)
    {
        return send("@wfm|cmd/unsubscribe/newOrders", filters(platform, crossplay));
    }

    /**
     * @brief Subscribe to one item's orders.
     *
     * Takes an item slug, e.g. `khora_prime_set`.
     * @throws std::logic_error Always: the route is not registered on the
     *         server, so nothing is sent.
     */
    void subscribeItemOrders(std::string_view) { unregisteredRoute(); }

    /**
     * @brief Subscribe to one user's orders.
     *
     * Takes an in-game name.
     * @throws std::logic_error Always: the route is not registered on the
     *         server, so nothing is sent.
     */
    void subscribeProfileOrders(std::string_view) { unregisteredRoute(); }

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
    void handle(std::string_view frame)
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
        if (route.ends_with(":ok"))
        {
            ++acknowledged_;
            return;
        }
        if (route.ends_with(":error"))
        {
            lastError_ = payloadText(message);
            return;
        }

        if (route == "@wfm|event/subscriptions/newOrder")
        {
            if (const auto payload = message.find("payload");
                payload != message.end() && payload->is_object())
            {
                const Order order = wfm_json::parseOrder(*payload);
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
    [[nodiscard]] const std::string &lastError() const { return lastError_; }

    /// Frames that were not valid message envelopes.
    [[nodiscard]] std::size_t malformedFrames() const { return malformed_; }

    /// Commands the server acknowledged.
    [[nodiscard]] std::size_t acknowledgedCommands() const { return acknowledged_; }

private:
    std::unique_ptr<WsTransport> transport_;
    NewOrderHandler newOrderHandler_;
    std::string lastError_;
    std::size_t malformed_ = 0;
    std::size_t acknowledged_ = 0;
    unsigned nextId_ = 1;

    static std::string payloadText(const nlohmann::json &message)
    {
        // Tolerant on purpose: an error frame with an unexpected payload shape
        // must not throw out of handle().
        const auto payload = message.find("payload");
        if (payload == message.end() || !payload->is_string())
        {
            return "unknown";
        }
        return payload->get<std::string>();
    }

    static nlohmann::json filters(std::string_view platform, bool crossplay)
    {
        nlohmann::json payload = {{"crossplay", crossplay}};
        if (!platform.empty())
        {
            payload["platform"] = platform;
        }
        return payload;
    }

    std::string send(std::string_view route, const nlohmann::json &payload)
    {
        const std::string id = "wft-" + std::to_string(nextId_++);
        transport_->sendText(
            nlohmann::json{{"route", route}, {"id", id}, {"payload", payload}}.dump());
        return id;
    }

    [[noreturn]] static void unregisteredRoute()
    {
        // shortcut: server route not registered, remove the throw when the docs
        // list the route as active.
        throw std::logic_error("route is not registered on the server");
    }
};

#endif // WFM_SOCKET_H
