#include <doctest.h>
#include <memory>
#include <string>
#include <vector>
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
    itemDB.clear();
}

std::unique_ptr<FakeTransport> makeTransport(FakeTransport *&out)
{
    std::unique_ptr<FakeTransport> transport(new FakeTransport());
    out = transport.get();
    return transport;
}

/// Item payload shaped like a real GET /v2/items entry.
nlohmann::json basicItem(const std::string &id, const std::string &slug)
{
    return {{"id", id}, {"slug", slug}, {"tradable", true}};
}

/// Load a single item so order calls have metadata to validate against.
void queueAndLoadItem(FakeTransport *fake, WarframeMarketAPI &api,
                      const nlohmann::json &item)
{
    fake->queueJson({{"apiVersion", "0.25.0"},
                     {"data", nlohmann::json::array({item})},
                     {"error", nullptr}});
    api.loadItems();
}

} // namespace

// ==================== Auth state ====================

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

// ==================== Item loading ====================

TEST_CASE("loadItems populates the lookup tables and reports the count")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson(
        {{"data",
          nlohmann::json::array({basicItem("id-khora", "khora_prime_set"),
                                 basicItem("id-ash", "ash_prime_set")})}});

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
                      doctest::Contains("invalid API response format"));
}

TEST_CASE("loadItems rejects an empty item list")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", nlohmann::json::array()}});
    CHECK_THROWS_WITH(api.loadItems(), doctest::Contains("contains no items"));
}

TEST_CASE("loadItems keeps item metadata needed for order validation")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", nlohmann::json::array(
                                   {{{"id", "id-relic"},
                                     {"slug", "axi_a1_relic"},
                                     {"tradable", true},
                                     {"bulkTradable", true},
                                     {"subtypes",
                                      nlohmann::json::array({"intact", "radiant"})},
                                     {"i18n", {{"en", {{"name", "Axi A1 Relic"}}}}}}})}});
    api.loadItems();

    const Item &item = api.getItem("axi_a1_relic");
    CHECK(item.itemName == "Axi A1 Relic");
    CHECK(item.requiresPerTrade());
    CHECK(item.requiresSubtype());
    CHECK_FALSE(item.requiresRank());
    REQUIRE(item.subtypes.size() == 2);
    CHECK(item.subtypes.at(1) == "radiant");
}

TEST_CASE("a failed load leaves the previous item index intact")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    REQUIRE(api.getItemCount() == 1);

    // Second load parses to nothing; the first index must survive.
    fake->queueJson({{"data", nlohmann::json::array()}});
    CHECK_THROWS(api.loadItems());

    CHECK(api.getItemCount() == 1);
    CHECK(api.getIDBySlug("khora_prime_set") == "id-khora");
}

TEST_CASE("searchItems only sees items already loaded")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson(
        {{"data",
          nlohmann::json::array({basicItem("id-khora", "khora_prime_set"),
                                 basicItem("id-ash", "ash_prime_set")})}});
    api.loadItems();

    CHECK(api.searchItems("khora").size() == 1);
    CHECK(api.searchItems("prime").size() == 2);
    CHECK(api.searchItems("nope").empty());
}

// ==================== Login ====================

TEST_CASE("login parses the token header and user payload")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"payload", {{"user", {{"id", "user-1"}, {"ingame_name", "Chew"}}}}}},
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

TEST_CASE("login finds the token header regardless of its capitalization")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    // Servers are free to spell header names however they like; a
    // case-sensitive lookup here would silently yield an empty token.
    fake->queueJson({{"payload", {{"user", {{"id", "u"}, {"ingame_name", "n"}}}}}},
                    {{"Authorization", "JWT mixed-case"}});

    const AuthResponse auth = api.login("a@b.c", "pw");
    CHECK(auth.token == "JWT mixed-case");
}

TEST_CASE("login fails clearly when the response carries no token")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    fake->queueJson({{"payload", {{"user", {{"id", "u"}, {"ingame_name", "n"}}}}}});

    CHECK_THROWS_WITH(api.login("a@b.c", "pw"),
                      doctest::Contains("no token in response"));
}

TEST_CASE("a token with the legacy JWT prefix is accepted by order calls")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));

    api.setJWT("JWT legacy-style");

    fake->queueJson({{"data", nlohmann::json::array()}, {"error", nullptr}});
    api.authenticated().getMyOrders();

    const FakeTransport::Request &request = fake->requests().at(1);
    CHECK(request.headers.at("Authorization") == "Bearer legacy-style");
}

// ==================== Order listing ====================

TEST_CASE("authenticated order listing maps item ids to slugs")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));

    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    fake->queueJson({{"data", nlohmann::json::array({{{"id", "order-1"},
                                                      {"type", "sell"},
                                                      {"platinum", 25},
                                                      {"quantity", 3},
                                                      {"itemId", "id-khora"},
                                                      {"visible", true}}})}});

    const std::vector<Order> orders = api.authenticated().getMyOrders();

    REQUIRE(orders.size() == 1);
    CHECK(orders[0].id == "order-1");
    CHECK(orders[0].orderType == "sell");
    CHECK(orders[0].platinum == 25);
    CHECK(orders[0].quantity == 3);
    CHECK(orders[0].itemSlug == "khora_prime_set");
    CHECK(orders[0].visible);

    const FakeTransport::Request &request = fake->requests().at(1);
    CHECK(request.url == V2_API + "/orders/my");
    CHECK(request.headers.at("Authorization") == "Bearer jwt-abc123");
}

TEST_CASE("order listing keeps the embedded owner status")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    fake->queueJson(
        {{"data", nlohmann::json::array({{{"id", "order-1"},
                                          {"type", "buy"},
                                          {"platinum", 5},
                                          {"quantity", 1},
                                          {"itemId", "id-khora"},
                                          {"visible", true},
                                          {"user",
                                           {{"id", "u1"},
                                            {"ingameName", "Trader"},
                                            {"status", "ingame"},
                                            {"reputation", 42}}}}})}});

    const std::vector<Order> orders = api.authenticated().getMyOrders();
    REQUIRE(orders.size() == 1);
    REQUIRE(orders[0].hasUser);
    CHECK(orders[0].user.status == "ingame");
    CHECK(orders[0].user.isOnline());
    CHECK(orders[0].user.reputation == 42);
}

// ==================== Order creation ====================

TEST_CASE("placeOrder sends a mapped item id and returns the new order id")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-42"}}}});

    PlaceOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.platinum = 30;
    request.quantity = 2;
    request.visible = true;

    const std::string orderId = api.authenticated().placeOrder(request);

    CHECK(orderId == "order-42");
    const FakeTransport::Request &sent = fake->requests().at(1);
    CHECK(sent.method == "POST");
    CHECK(sent.url == V2_API + "/order");
    CHECK(sent.payload.at("itemId") == "id-khora");
    CHECK(sent.payload.at("type") == "sell");
    CHECK(sent.payload.at("platinum") == 30);
    CHECK_FALSE(sent.payload.contains("rank"));
    CHECK_FALSE(sent.payload.contains("perTrade"));
}

TEST_CASE("placeOrder sends a buy order when asked")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");
    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-9"}}}});

    PlaceOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.type = OrderType::Buy;
    request.platinum = 12;
    request.quantity = 1;

    api.authenticated().placeOrder(request);
    CHECK(fake->requests().at(1).payload.at("type") == "buy");
}

TEST_CASE("placeOrder includes rank only when requested")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api,
                     {{"id", "id-mod"}, {"slug", "serration"}, {"maxRank", 10}});
    api.setJWT("jwt-abc123");
    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-7"}}}});

    PlaceOrderRequest request;
    request.itemSlug = "serration";
    request.platinum = 5;
    request.quantity = 1;
    request.hasRank = true;
    request.rank = 3;

    api.authenticated().placeOrder(request);
    CHECK(fake->requests().at(1).payload.at("rank") == 3);
}

TEST_CASE("placeOrder requires rank for a ranked item")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api,
                     {{"id", "id-mod"}, {"slug", "serration"}, {"maxRank", 10}});
    api.setJWT("jwt-abc123");

    PlaceOrderRequest request;
    request.itemSlug = "serration";
    request.platinum = 5;
    request.quantity = 1;

    CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                    order_validation::ValidationError);
}

TEST_CASE("placeOrder requires perTrade and a valid subtype for a relic")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api,
                     {{"id", "id-relic"},
                      {"slug", "axi_a1_relic"},
                      {"bulkTradable", true},
                      {"subtypes", nlohmann::json::array({"intact", "radiant"})}});
    api.setJWT("jwt-abc123");

    SUBCASE("omitting perTrade is rejected before any request is sent")
    {
        PlaceOrderRequest request;
        request.itemSlug = "axi_a1_relic";
        request.platinum = 10;
        request.quantity = 6;
        request.hasSubtype = true;
        request.subtype = "intact";

        CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                        order_validation::ValidationError);
        CHECK(fake->requests().size() == 1); // only the item load
    }

    SUBCASE("an unknown subtype is rejected")
    {
        PlaceOrderRequest request;
        request.itemSlug = "axi_a1_relic";
        request.platinum = 10;
        request.quantity = 6;
        request.hasPerTrade = true;
        request.perTrade = 6;
        request.hasSubtype = true;
        request.subtype = "flawless";

        CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                        order_validation::ValidationError);
    }

    SUBCASE("a complete request is sent")
    {
        fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-77"}}}});

        PlaceOrderRequest request;
        request.itemSlug = "axi_a1_relic";
        request.platinum = 10;
        request.quantity = 6;
        request.hasPerTrade = true;
        request.perTrade = 6;
        request.hasSubtype = true;
        request.subtype = "intact";

        CHECK(api.authenticated().placeOrder(request) == "order-77");
        const nlohmann::json &sent = fake->requests().at(1).payload;
        CHECK(sent.at("perTrade") == 6);
        CHECK(sent.at("subtype") == "intact");
    }
}

TEST_CASE("placeOrder rejects fields the item does not support")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    PlaceOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.platinum = 10;
    request.quantity = 1;
    request.hasRank = true; // set parts have no rank
    request.rank = 0;

    CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                    order_validation::ValidationError);
}

TEST_CASE("placeOrder rejects an unknown slug without sending a request")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");

    PlaceOrderRequest request;
    request.itemSlug = "never_loaded";
    request.platinum = 10;
    request.quantity = 1;

    CHECK_THROWS_WITH(api.authenticated().placeOrder(request),
                      doctest::Contains("loadItems"));
    CHECK(fake->requests().empty());
}

TEST_CASE("placeOrder requires rank and subtype together when the item needs both")
{
    // Real `serration` carries both maxRank and subtypes, so a realistic order
    // has to supply rank and subtype at once. The single-rule tests above use
    // simplified fixtures and would not catch an interaction bug.
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api,
                     {{"id", "id-serration"},
                      {"slug", "serration"},
                      {"maxRank", 10},
                      {"subtypes", nlohmann::json::array({"regular", "atragraph"})}});
    api.setJWT("jwt-abc123");

    SUBCASE("rank without subtype is rejected")
    {
        PlaceOrderRequest request;
        request.itemSlug = "serration";
        request.platinum = 5;
        request.quantity = 1;
        request.hasRank = true;
        request.rank = 3;

        CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                        order_validation::ValidationError);
        CHECK(fake->requests().size() == 1); // only the item load
    }

    SUBCASE("subtype without rank is rejected")
    {
        PlaceOrderRequest request;
        request.itemSlug = "serration";
        request.platinum = 5;
        request.quantity = 1;
        request.hasSubtype = true;
        request.subtype = "regular";

        CHECK_THROWS_AS(api.authenticated().placeOrder(request),
                        order_validation::ValidationError);
    }

    SUBCASE("both together are sent")
    {
        fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-8"}}}});

        PlaceOrderRequest request;
        request.itemSlug = "serration";
        request.platinum = 5;
        request.quantity = 1;
        request.hasRank = true;
        request.rank = 3;
        request.hasSubtype = true;
        request.subtype = "regular";

        CHECK(api.authenticated().placeOrder(request) == "order-8");
        const nlohmann::json &sent = fake->requests().at(1).payload;
        CHECK(sent.at("rank") == 3);
        CHECK(sent.at("subtype") == "regular");
    }
}

TEST_CASE("placeOrder surfaces API error codes from the envelope")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    fake->queueJson(
        {{"apiVersion", "0.25.0"},
         {"data", nullptr},
         {"error", {{"request", nlohmann::json::array({"app.errors.unauthorized"})}}}});

    PlaceOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.platinum = 10;
    request.quantity = 1;

    CHECK_THROWS_WITH(api.authenticated().placeOrder(request),
                      doctest::Contains("app.errors.unauthorized"));
}

TEST_CASE("field-level error codes are reported with their field name")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    queueAndLoadItem(fake, api, basicItem("id-khora", "khora_prime_set"));
    api.setJWT("jwt-abc123");

    fake->queueJson({{"data", nullptr},
                     {"error",
                      {{"request", nlohmann::json::array()},
                       {"inputs", {{"platinum", "app.field.tooSmall"}}}}}});

    PlaceOrderRequest request;
    request.itemSlug = "khora_prime_set";
    request.platinum = 10;
    request.quantity = 1;

    try
    {
        api.authenticated().placeOrder(request);
        FAIL("expected ApiError");
    }
    catch (const ApiError &e)
    {
        REQUIRE(e.inputCodes().size() == 1);
        CHECK(e.inputCodes().at(0) == "platinum=app.field.tooSmall");
    }
}

// ==================== Order update / close / delete ====================

TEST_CASE("updateOrder rejects a request with no fields set")
{
    resetGlobalItemDb();
    WarframeMarketAPI api(std::unique_ptr<HttpTransport>(new FakeTransport()));
    api.setJWT("jwt-abc123");

    UpdateOrderRequest empty;
    CHECK_THROWS_WITH(api.authenticated().updateOrder("order-1", empty),
                      doctest::Contains("No fields provided"));
}

TEST_CASE("updateOrder sends only the fields that were set")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");
    fake->queueJson({{"error", nullptr}, {"data", {{"id", "order-1"}}}});

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

TEST_CASE("updateOrder returns the updated order")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");
    fake->queueJson({{"error", nullptr},
                     {"data", {{"id", "order-1"}, {"type", "sell"}, {"platinum", 11}}}});

    UpdateOrderRequest request;
    request.hasPlatinum = true;
    request.platinum = 11;

    const Order updated = api.authenticated().updateOrder("order-1", request);
    CHECK(updated.id == "order-1");
    CHECK(updated.platinum == 11);
}

TEST_CASE("closeOrder posts the quantity and returns the transaction")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");
    fake->queueJson({{"error", nullptr},
                     {"data", {{"id", "tx-1"}, {"originId", "order-1"}, {"quantity", 2}}}});

    const Transaction tx = api.authenticated().closeOrder("order-1", 2);

    const FakeTransport::Request &sent = fake->requests().at(0);
    CHECK(sent.url == V2_API + "/order/order-1/close");
    CHECK(sent.payload.at("quantity") == 2);
    CHECK(tx.id == "tx-1");
    CHECK(tx.originId == "order-1");
}

TEST_CASE("closeOrder rejects a non-positive quantity before sending")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");

    CHECK_THROWS_WITH(api.authenticated().closeOrder("order-1", 0),
                      doctest::Contains("at least 1"));
    CHECK(fake->requests().empty());
}

TEST_CASE("deleteOrder succeeds on 200 and throws otherwise")
{
    resetGlobalItemDb();
    FakeTransport *fake = nullptr;
    WarframeMarketAPI api(makeTransport(fake));
    api.setJWT("jwt-abc123");

    fake->queueReply("", FakeTransport::Headers(), 200);
    CHECK_NOTHROW(api.authenticated().deleteOrder("order-1"));

    fake->queueReply("", FakeTransport::Headers(), 404);
    CHECK_THROWS_WITH(api.authenticated().deleteOrder("order-2"),
                      doctest::Contains("HTTP 404"));
}

// ==================== Error envelope handling ====================

TEST_CASE("error codes are found whether nested under error or at the top level")
{
    const nlohmann::json nested = {
        {"data", nullptr},
        {"error", {{"request", nlohmann::json::array({"app.errors.forbidden"})}}}};
    CHECK_THROWS_WITH(throwIfApiError(nested, "ctx"),
                      doctest::Contains("app.errors.forbidden"));

    const nlohmann::json flat = {
        {"data", nullptr},
        {"request", nlohmann::json::array({"app.errors.forbidden"})}};
    CHECK_THROWS_WITH(throwIfApiError(flat, "ctx"),
                      doctest::Contains("app.errors.forbidden"));
}

TEST_CASE("a successful envelope is not mistaken for an error")
{
    const nlohmann::json ok = {{"data", {{"id", "x"}}}, {"error", nullptr}};
    CHECK_NOTHROW(throwIfApiError(ok, "ctx"));
}
