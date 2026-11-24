#include <doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "fake_transport.h"
#include "market_api.h"

// MarketAPI is the anonymous read surface: no token, so tests queue replies and
// check both the request shape and the parsed models.

namespace
{

std::unique_ptr<FakeTransport> makeTransport(FakeTransport *&out)
{
    std::unique_ptr<FakeTransport> transport(new FakeTransport());
    out = transport.get();
    return transport;
}

nlohmann::json orderJson(const std::string &id, const std::string &type, int platinum,
                         int quantity, int perTrade = 1)
{
    return {{"id", id},
            {"type", type},
            {"platinum", platinum},
            {"quantity", quantity},
            {"perTrade", perTrade},
            {"visible", true},
            {"itemId", "item-1"},
            {"user", {{"id", "u1"}, {"status", "ingame"}}}};
}

} // namespace

TEST_CASE("MarketAPI rejects a null transport")
{
    CHECK_THROWS_AS(MarketAPI(std::unique_ptr<HttpTransport>()), std::invalid_argument);
}

TEST_CASE("items() returns full item models")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"apiVersion", "0.25.0"},
                     {"data", nlohmann::json::array({{{"id", "i1"},
                                                      {"slug", "khora_prime_set"},
                                                      {"tradable", true},
                                                      {"tradingTax", 8000},
                                                      {"i18n",
                                                       {{"en", {{"name", "Khora Prime Set"}}}}}}})},
                     {"error", nullptr}});

    const std::vector<Item> items = api.items();

    REQUIRE(items.size() == 1);
    CHECK(items[0].slug == "khora_prime_set");
    CHECK(items[0].itemName == "Khora Prime Set");
    CHECK(items[0].tradingTax == 8000);
    CHECK(fake->requests().at(0).url == V2_API + "/items");
}

TEST_CASE("requests carry platform, language, crossplay, and a real User-Agent")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));
    api.setContext("xbox", "de", false);

    fake->queueJson({{"data", nlohmann::json::array()}, {"error", nullptr}});
    api.items();

    const FakeTransport::Request &request = fake->requests().at(0);
    CHECK(request.headers.at("Platform") == "xbox");
    CHECK(request.headers.at("Language") == "de");
    CHECK(request.headers.at("Crossplay") == "false");
    // The API rules require clients to identify themselves.
    CHECK(request.headers.at("User-Agent") == WFT_USER_AGENT);
}

TEST_CASE("item() maps a 404 body to the documented error code")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueReply(
        nlohmann::json{{"data", nullptr},
                       {"error", {{"request", nlohmann::json::array({"app.item.notFound"})}}}}
            .dump(),
        FakeTransport::Headers(), 404);

    CHECK_THROWS_WITH(api.item("nope"), doctest::Contains("app.item.notFound"));
}

TEST_CASE("ordersForItem parses bundle metadata")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", nlohmann::json::array({orderJson("o1", "sell", 12, 24, 1),
                                                     orderJson("o2", "buy", 30, 6, 6)})},
                     {"error", nullptr}});

    const std::vector<Order> orders = api.ordersForItem("axi_a1_relic");

    REQUIRE(orders.size() == 2);
    CHECK(orders[0].perTrade == 1);
    // 30 platinum over a bundle of 6 is 5 per unit.
    CHECK(orders[1].platinumPerUnit() == doctest::Approx(5.0));
    CHECK(fake->requests().at(0).url == V2_API + "/orders/item/axi_a1_relic");
}

TEST_CASE("bookForItem builds a ranked book in one call")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", nlohmann::json::array({orderJson("pricey", "sell", 20, 1, 1),
                                                     orderJson("cheap", "sell", 36, 6, 6)})},
                     {"error", nullptr}});

    const OrderBook book = api.bookForItem("axi_a1_relic");

    double best = 0.0;
    REQUIRE(book.bestSell(best));
    CHECK(best == doctest::Approx(6.0));
}

TEST_CASE("topOrdersForItem splits the sides and applies the rank filter")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson(
        {{"data",
          {{"sell", nlohmann::json::array({orderJson("s", "sell", 70, 1)})},
           {"buy", nlohmann::json::array({orderJson("b", "buy", 60, 1)})}}},
         {"error", nullptr}});

    const MarketAPI::TopOrders top = api.topOrdersForItem("khora_prime_set", 3);

    CHECK(fake->requests().at(0).url == V2_API + "/orders/item/khora_prime_set/top?rank=3");
    REQUIRE(top.sell.size() == 1);
    REQUIRE(top.buy.size() == 1);
    CHECK(top.sell[0].id == "s");
    CHECK(top.buy[0].id == "b");
}

TEST_CASE("rankLt takes precedence over rank, as the API does")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", {{"sell", nlohmann::json::array()},
                               {"buy", nlohmann::json::array()}}},
                     {"error", nullptr}});

    api.topOrdersForItem("serration", 3, 5);
    CHECK(fake->requests().at(0).url == V2_API + "/orders/item/serration/top?rankLt=5");
}

TEST_CASE("topOrdersForItem omits the query when no rank is requested")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", {{"sell", nlohmann::json::array()},
                               {"buy", nlohmann::json::array()}}},
                     {"error", nullptr}});

    api.topOrdersForItem("serration");
    CHECK(fake->requests().at(0).url == V2_API + "/orders/item/serration/top");
}

TEST_CASE("recentOrders parses the marketplace-wide feed")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson(
        {{"data", nlohmann::json::array({orderJson("r1", "buy", 20, 1)})}, {"error", nullptr}});

    const std::vector<Order> orders = api.recentOrders();

    REQUIRE(orders.size() == 1);
    CHECK(orders[0].id == "r1");
    CHECK(fake->requests().at(0).url == V2_API + "/orders/recent");
}

TEST_CASE("a non-2xx without an error envelope reports the status")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueReply("gateway exploded", FakeTransport::Headers(), 502);
    CHECK_THROWS_WITH(api.items(), doctest::Contains("HTTP 502"));
}

TEST_CASE("invalid JSON is reported rather than returning empty")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueReply("<html>not json</html>");
    CHECK_THROWS_WITH(api.items(), doctest::Contains("not valid JSON"));
}
