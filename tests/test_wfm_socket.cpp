#include <doctest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fake_ws_transport.h"
#include "wfm_socket.h"

// WfmSocket owns envelope handling and dispatch. Canned frames cover the
// protocol without a socket.

namespace
{

template <typename Fake>
std::unique_ptr<Fake> makeTransport(Fake *&out)
{
    auto transport = std::make_unique<Fake>();
    out = transport.get();
    return transport;
}

std::string newOrderFrame(const std::string &id, const std::string &type, int platinum,
                          int quantity, bool visible = true)
{
    return nlohmann::json{
        {"route", "@wfm|event/subscriptions/newOrder"},
        {"payload", {{"id", id},
                     {"type", type},
                     {"platinum", platinum},
                     {"quantity", quantity},
                     {"itemId", "item-1"},
                     {"visible", visible},
                     {"user", {{"id", "u1"}, {"status", "ingame"}}}}}}
        .dump();
}

} // namespace

TEST_CASE("connect uses the wss endpoint and the wfm subprotocol")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));

    socket.connect();

    CHECK(fake->connectCount() == 1);
    CHECK(fake->url() == WS_API);
    // The server rejects connections without this subprotocol.
    CHECK(fake->subprotocol() == "wfm");
    CHECK(socket.isConnected());
}

TEST_CASE("subscribeNewOrders sends a correlatable command")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    const std::string id = socket.subscribeNewOrders("pc", true);

    REQUIRE(fake->sent().size() == 1);
    const nlohmann::json sent = nlohmann::json::parse(fake->sent().at(0));
    CHECK(sent.at("route") == "@wfm|cmd/subscribe/newOrders");
    CHECK(sent.at("id") == id);
    CHECK(sent.at("payload").at("platform") == "pc");
    CHECK(sent.at("payload").at("crossplay") == true);
}

TEST_CASE("an omitted platform subscribes across all platforms")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    socket.subscribeNewOrders();

    const nlohmann::json sent = nlohmann::json::parse(fake->sent().at(0));
    CHECK_FALSE(sent.at("payload").contains("platform"));
}

TEST_CASE("unsubscribe resends the same filters")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    socket.subscribeNewOrders("pc", true);
    socket.unsubscribeNewOrders("pc", true);

    const nlohmann::json sent = nlohmann::json::parse(fake->sent().at(1));
    CHECK(sent.at("route") == "@wfm|cmd/unsubscribe/newOrders");
    CHECK(sent.at("payload").at("platform") == "pc");
    CHECK(sent.at("payload").at("crossplay") == true);
}

TEST_CASE("new-order events are dispatched to the handler")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    std::vector<Order> received;
    socket.onNewOrder([&received](const Order &order) { received.push_back(order); });

    fake->queueFrame(newOrderFrame("o1", "sell", 20, 3));

    CHECK(socket.poll(1));
    REQUIRE(received.size() == 1);
    CHECK(received[0].id == "o1");
    CHECK(received[0].platinum == 20);
    REQUIRE(received[0].user);
    CHECK(received[0].user->isOnline());
}

TEST_CASE("hidden new-order events are not dispatched")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    size_t calls = 0;
    socket.onNewOrder([&calls](const Order &) { ++calls; });

    fake->queueFrame(newOrderFrame("o1", "sell", 20, 1, false));

    socket.poll(1);
    CHECK(calls == 0);
}

TEST_CASE("a command acknowledgement is counted, not dispatched")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    size_t orderCalls = 0;
    socket.onNewOrder([&orderCalls](const Order &) { ++orderCalls; });

    fake->queueFrame(
        nlohmann::json{{"route", "@wfm|cmd/subscribe/newOrders:ok"}, {"id", "wft-1"}}.dump());

    socket.poll(1);
    CHECK(socket.acknowledgedCommands() == 1);
    CHECK(orderCalls == 0);
}

TEST_CASE("command and protection errors are captured verbatim")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    fake->queueFrame(nlohmann::json{{"route", "@wfm|cmd/subscribe/newOrders:error"},
                                    {"payload", "app.errors.alreadySubscribed"}}
                         .dump());
    socket.poll(1);
    CHECK(socket.lastError() == "app.errors.alreadySubscribed");

    fake->queueFrame(
        nlohmann::json{{"route", "@wfm|protect/error"}, {"payload", "app.errors.userNotVerified"}}
            .dump());
    socket.poll(1);
    CHECK(socket.lastError() == "app.errors.userNotVerified");
}

TEST_CASE("an error frame carrying no payload is reported as unknown, not empty")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    fake->queueFrame(nlohmann::json{{"route", "@wfm|cmd/subscribe/newOrders:error"}}.dump());

    CHECK_NOTHROW(socket.poll(1));
    CHECK(socket.lastError() == "unknown");
}

TEST_CASE("an error frame with a non-string payload is tolerated")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    // An unexpected shape must not throw out of handle(): the connection would
    // die on a frame the server is allowed to send.
    fake->queueFrame(nlohmann::json{{"route", "@wfm|cmd/subscribe/newOrders:error"},
                                    {"payload", {{"code", "app.errors.unknown"}}}}
                         .dump());

    CHECK_NOTHROW(socket.poll(1));
    CHECK(socket.lastError() == "unknown");
}

TEST_CASE("malformed frames are counted without killing the connection")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    std::vector<Order> received;
    socket.onNewOrder([&received](const Order &order) { received.push_back(order); });

    fake->queueFrame("not json at all");
    socket.poll(1);
    fake->queueFrame(nlohmann::json{{"no_route", true}}.dump());
    socket.poll(1);
    fake->queueFrame(newOrderFrame("o2", "buy", 5, 1));
    socket.poll(1);

    CHECK(socket.malformedFrames() == 2);
    REQUIRE(received.size() == 1);
    CHECK(received[0].id == "o2");
}

TEST_CASE("unknown routes are ignored rather than fatal")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    // The backend adds routes over time; a strict client would start throwing.
    fake->queueFrame(
        nlohmann::json{{"route", "@wfm|event/something/new"}, {"payload", {{"x", 1}}}}.dump());

    CHECK_NOTHROW(socket.poll(1));
    CHECK(socket.malformedFrames() == 0);
}

TEST_CASE("poll reports zero when no frame arrives")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    CHECK_FALSE(socket.poll(1));
}

TEST_CASE("item and profile order subscriptions are refused before sending")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));
    socket.connect();

    // The server ships these routes as stubs, so nothing may reach the wire.
    CHECK_THROWS_WITH(socket.subscribeItemOrders("khora_prime_set"),
                      doctest::Contains("not registered on the server"));
    CHECK_THROWS_WITH(socket.subscribeProfileOrders("some-user"),
                      doctest::Contains("not registered on the server"));
    CHECK(fake->sent().empty());
}

TEST_CASE("sending on a closed socket throws")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));

    CHECK_THROWS_WITH(socket.subscribeNewOrders(), doctest::Contains("closed connection"));
}

TEST_CASE("WfmSocket rejects a null transport")
{
    CHECK_THROWS_AS(WfmSocket(std::unique_ptr<WsTransport>()), std::invalid_argument);
}

TEST_CASE("handle() dispatches a frame from a caller-owned socket")
{
    // The documented way to use the protocol layer without a WsTransport that
    // reads: feed frames yourself. No connect() is involved.
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));

    std::vector<Order> received;
    socket.onNewOrder([&received](const Order &order) { received.push_back(order); });

    socket.handle(newOrderFrame("muxed", "sell", 40, 2));

    CHECK_FALSE(socket.isConnected());
    REQUIRE(received.size() == 1);
    CHECK(received[0].id == "muxed");
}

TEST_CASE("handle() accepts a frame without copying it into a std::string")
{
    FakeWsTransport *fake = nullptr;
    WfmSocket socket(makeTransport(fake));

    std::vector<Order> received;
    socket.onNewOrder([&received](const Order &order) { received.push_back(order); });

    const std::string frame = newOrderFrame("view", "sell", 3, 1);
    socket.handle(std::string_view(frame));

    REQUIRE(received.size() == 1);
    CHECK(received[0].id == "view");
}
