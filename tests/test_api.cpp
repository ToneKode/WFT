#include <doctest.h>
#include <memory>
#include "fake_transport.h"
#include "wfm_api.h"

// These tests drive WarframeMarketAPI through FakeTransport, so the whole
// request -> parse -> model path is covered without network or credentials.
// The item database is a process-wide global, so tests that populate it reset
// it first.

namespace
{

void resetGlobalItemDb()
{
    itemDB.idToSlug.clear();
    itemDB.slugToId.clear();
    itemDB.allSlugs.clear();
}

std::unique_ptr<FakeTransport> makeTransport(FakeTransport *&out)
{
    std::unique_ptr<FakeTransport> transport(new FakeTransport());
    out = transport.get();
    return transport;
}

} // namespace

TEST_CASE("unauthenticated client reports state and refuses order calls")
{
    resetGlobalItemDb();
    WarframeMarketAPI api(std::unique_ptr<HttpTransport>(new FakeTransport()));

    CHECK_FALSE(api.isAuthenticated());
    CHECK_THROWS_WITH(api.authenticated(),
                      doctest::Contains("Authentication required"));
}

TEST_CASE("setJWT flips authentication state in both directions")
{
    resetGlobalItemDb();
    WarframeMarketAPI api(std::unique_ptr<HttpTransport>(new FakeTransport()));

    api.setJWT("JWT some-token");
    CHECK(api.isAuthenticated());
    CHECK(api.getJWT() == "JWT some-token");

    api.clearJWT();
    CHECK_FALSE(api.isAuthenticated());
    CHECK(api.getJWT().empty());
}

TEST_CASE("setJWT with an empty string clears authentication")
{
    resetGlobalItemDb();
    WarframeMarketAPI api(std::unique_ptr<HttpTransport>(new FakeTransport()));

    api.setJWT("JWT some-token");
    api.setJWT("");
    CHECK_FALSE(api.isAuthenticated());
}

TEST_CASE("loadItems populates the lookup tables and reports the count")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-khora"}, {"slug", "khora_prime_set"}},
                       {{"id", "id-ash"}, {"slug", "ash_prime_set"}}}}});

    const size_t count = api.loadItems();

    CHECK(count == 2);
    CHECK(api.getItemCount() == 2);
    CHECK(api.getSlugByID("id-khora") == "khora_prime_set");
    CHECK(api.getIDBySlug("ash_prime_set") == "id-ash");
    REQUIRE(fake->requests().size() == 1);
    CHECK(fake->requests().at(0).method == "GET");
    CHECK(fake->requests().at(0).url == V2_API + "/items");
}

TEST_CASE("loadItems rejects responses that are not a data array")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", "not-an-array"}});
    CHECK_THROWS_WITH(api.loadItems(),
                      doctest::Contains("Invalid API response format"));
}

TEST_CASE("loadItems rejects an empty item list")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", nlohmann::json::array()}});
    CHECK_THROWS_WITH(api.loadItems(),
                      doctest::Contains("contains no items"));
}

TEST_CASE("searchItems only sees items already loaded")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-khora"}, {"slug", "khora_prime_set"}},
                       {{"id", "id-ash"}, {"slug", "ash_prime_set"}}}}});
    api.loadItems();

    CHECK(api.searchItems("khora").size() == 1);
    CHECK(api.searchItems("prime").size() == 2);
    CHECK(api.searchItems("nope").empty());
}

TEST_CASE("login parses the token header and user payload")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"payload", {{"user", {{"id", "user-1"},
                                           {"ingame_name", "Chew"}}}}}},
                    {{"authorization", "JWT abc123"}});

    const AuthResponse auth = api.login("chew@example.com", "hunter2", "device-9");

    CHECK(auth.token == "JWT abc123");
    CHECK(auth.user_id == "user-1");
    CHECK(auth.ingame_name == "Chew");

    REQUIRE(fake->requests().size() == 1);
    const FakeTransport::Request &request = fake->requests().at(0);
    CHECK(request.method == "POST");
    CHECK(request.url == V1_API + "/auth/signin");
    CHECK(request.payload.at("email") == "chew@example.com");
    CHECK(request.payload.at("auth_type") == "header");
}

TEST_CASE("authenticated order listing maps item ids to slugs")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-khora"}, {"slug", "khora_prime_set"}}}}});
    api.loadItems();
    api.setJWT("JWT abc123");

    fake->queueJson({{"data",
                      {{{"id", "order-1"},
                        {"type", "sell"},
                        {"platinum", 25},
                        {"quantity", 3},
                        {"itemId", "id-khora"},
                        {"visible", true}}}}});

    const std::vector<OrderDetail> orders = api.authenticated().getMyOrders();

    REQUIRE(orders.size() == 1);
    CHECK(orders[0].id == "order-1");
    CHECK(orders[0].orderType == "sell");
    CHECK(orders[0].platinum == 25);
    CHECK(orders[0].quantity == 3);
    CHECK(orders[0].itemSlug == "khora_prime_set");
    CHECK(orders[0].visible);

    const FakeTransport::Request &request = fake->requests().at(1);
    CHECK(request.url == V2_API + "/orders/my");
    CHECK(request.headers.at("Authorization") == "Bearer abc123");
}

TEST_CASE("createSellOrder sends a mapped item id and returns the new order id")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-khora"}, {"slug", "khora_prime_set"}}}}});
    api.loadItems();
    api.setJWT("JWT abc123");

    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-42"}}}});

    SellOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.platinum = 30;
    request.quantity = 2;
    request.visible = true;

    const std::string orderId = api.authenticated().createSellOrder(request);

    CHECK(orderId == "order-42");
    const FakeTransport::Request &sent = fake->requests().at(1);
    CHECK(sent.method == "POST");
    CHECK(sent.url == V2_API + "/order/");
    CHECK(sent.payload.at("itemId") == "id-khora");
    CHECK(sent.payload.at("type") == "sell");
    CHECK(sent.payload.at("platinum") == 30);
    CHECK_FALSE(sent.payload.contains("rank"));
}

TEST_CASE("createSellOrder includes rank only when requested")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-mod"}, {"slug", "serration"}}}}});
    api.loadItems();
    api.setJWT("JWT abc123");
    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-7"}}}});

    SellOrderRequest request;
    request.itemSlug = "serration";
    request.platinum = 5;
    request.quantity = 1;
    request.hasRank = true;
    request.rank = 3;

    api.authenticated().createSellOrder(request);

    CHECK(fake->requests().at(1).payload.at("rank") == 3);
}

TEST_CASE("createSellOrder surfaces API errors")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data",
                      {{{"id", "id-khora"}, {"slug", "khora_prime_set"}}}}});
    api.loadItems();
    api.setJWT("JWT abc123");
    fake->queueJson({{"error", "not enough platinum"}});

    SellOrderRequest request;
    request.itemSlug = "khora_prime_set";

    CHECK_THROWS_WITH(api.authenticated().createSellOrder(request),
                      doctest::Contains("not enough platinum"));
}

TEST_CASE("updateOrder rejects a request with no fields set")
{
    resetGlobalItemDb();
    WarframeMarketAPI api(std::unique_ptr<HttpTransport>(new FakeTransport()));
    api.setJWT("JWT abc123");

    UpdateOrderRequest empty;
    CHECK_THROWS_WITH(api.authenticated().updateOrder("order-1", empty),
                      doctest::Contains("No fields provided"));
}

TEST_CASE("updateOrder sends only the fields that were set")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("JWT abc123");
    fake->queueJson({{"error", nullptr}, {"data", nlohmann::json::object()}});

    UpdateOrderRequest request;
    request.hasPlatinum = true;
    request.platinum = 11;

    api.authenticated().updateOrder("order-1", request);

    const FakeTransport::Request &sent = fake->requests().at(0);
    CHECK(sent.method == "PATCH");
    CHECK(sent.url == V2_API + "/order/order-1");
    CHECK(sent.payload.at("platinum") == 11);
    CHECK_FALSE(sent.payload.contains("quantity"));
    CHECK_FALSE(sent.payload.contains("visible"));
    CHECK_FALSE(sent.payload.contains("rank"));
}

TEST_CASE("closeOrder posts the quantity to the close endpoint")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("JWT abc123");
    fake->queueJson({{"error", nullptr}});

    api.authenticated().closeOrder("order-1", 2);

    const FakeTransport::Request &sent = fake->requests().at(0);
    CHECK(sent.url == V2_API + "/order/order-1/close");
    CHECK(sent.payload.at("quantity") == 2);
}

TEST_CASE("deleteOrder succeeds on 200 and throws otherwise")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("JWT abc123");

    fake->queueReply("", FakeTransport::Headers(), 200);
    CHECK_NOTHROW(api.authenticated().deleteOrder("order-1"));

    fake->queueReply("", FakeTransport::Headers(), 404);
    CHECK_THROWS_WITH(api.authenticated().deleteOrder("order-2"),
                      doctest::Contains("HTTP 404"));
}
