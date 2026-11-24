#include <doctest.h>
#include "order_book.h"

// The price normalisation here is load-bearing: `platinum` is a bundle total,
// so a book that ranked on raw platinum would mis-price every bulk listing.

namespace
{

Order makeOrder(const std::string &id, const std::string &type, int platinum, int quantity,
                int perTrade = 0, const std::string &status = "ingame", bool visible = true)
{
    Order order;
    order.id = id;
    order.orderType = type;
    order.platinum = platinum;
    order.quantity = quantity;
    order.visible = visible;
    if (perTrade > 0)
    {
        order.hasPerTrade = true;
        order.perTrade = perTrade;
    }
    order.hasUser = true;
    order.user.status = status;
    return order;
}

} // namespace

TEST_CASE("platinumPerUnit divides a bundle price by perTrade")
{
    // Live shape: 30 platinum for a bundle of 6 is 5 per unit.
    CHECK(makeOrder("a", "buy", 30, 6, 6).platinumPerUnit() == doctest::Approx(5.0));
    CHECK(makeOrder("b", "buy", 5, 1).platinumPerUnit() == doctest::Approx(5.0));
}

TEST_CASE("bulk sellers are not made to look expensive by raw price")
{
    // Sorting on platinum would rank the 36p bundle worst, though it is cheapest.
    OrderBook book;
    book.load({makeOrder("bulk", "sell", 36, 6, 6),      // 6 per unit
               makeOrder("single", "sell", 10, 1)});     // 10 per unit

    double best = 0.0;
    REQUIRE(book.bestSell(best));
    CHECK(best == doctest::Approx(6.0));
    CHECK(book.sells().front().id == "bulk");
}

TEST_CASE("sells sort cheapest first and buys highest first")
{
    OrderBook book;
    book.load({makeOrder("s1", "sell", 20, 1), makeOrder("s2", "sell", 10, 1),
               makeOrder("b1", "buy", 15, 1), makeOrder("b2", "buy", 25, 1)});

    REQUIRE(book.sells().size() == 2);
    CHECK(book.sells()[0].id == "s2");
    CHECK(book.buys()[0].id == "b2");
}

TEST_CASE("equal unit prices prefer the deeper order")
{
    OrderBook book;
    book.load({makeOrder("shallow", "sell", 10, 1), makeOrder("deep", "sell", 10, 50)});

    CHECK(book.sells().front().id == "deep");
}

TEST_CASE("hidden orders are excluded")
{
    OrderBook book;
    book.load({makeOrder("visible", "sell", 10, 1),
               makeOrder("hidden", "sell", 1, 1, 0, "ingame", false)});

    REQUIRE(book.sells().size() == 1);
    CHECK(book.sells().front().id == "visible");
}

TEST_CASE("offline users do not set the price")
{
    // The server filters offline users from /top; a book that let them through
    // would disagree with the endpoint it mirrors.
    OrderBook book;
    book.load({makeOrder("offline", "sell", 1, 1, 0, "offline"),
               makeOrder("online", "sell", 50, 1)});

    double best = 0.0;
    REQUIRE(book.bestSell(best));
    CHECK(best == doctest::Approx(50.0));
}

TEST_CASE("orders without an embedded user are not filtered out")
{
    // /orders/my omits the owner, so absence of a user is not proof of being
    // offline.
    Order owned;
    owned.id = "mine";
    owned.orderType = "sell";
    owned.platinum = 7;
    owned.quantity = 1;
    owned.visible = true;

    OrderBook book;
    book.load({owned});
    CHECK(book.sells().size() == 1);
}

TEST_CASE("spread reports the gap between best buy and best sell")
{
    OrderBook book;
    book.load({makeOrder("s", "sell", 20, 1), makeOrder("b", "buy", 15, 1)});

    double out = 0.0;
    REQUIRE(book.spread(out));
    CHECK(out == doctest::Approx(5.0));
}

TEST_CASE("spread is unavailable when a side is empty, and negative when crossed")
{
    OrderBook book;
    book.load({makeOrder("s", "sell", 20, 1)});

    double out = 0.0;
    CHECK_FALSE(book.spread(out));
    CHECK_FALSE(book.bestBuy(out));
    CHECK(book.bestSell(out));

    book.load({makeOrder("s", "sell", 10, 1), makeOrder("b", "buy", 12, 1)});
    REQUIRE(book.spread(out));
    CHECK(out < 0.0);
}

TEST_CASE("loading again replaces the previous book")
{
    OrderBook book;
    book.load({makeOrder("a", "sell", 5, 1)});
    REQUIRE(book.sells().size() == 1);

    book.load({makeOrder("b", "buy", 5, 1)});
    CHECK(book.sells().empty());
    CHECK(book.buys().size() == 1);
}

TEST_CASE("an empty list produces an empty book")
{
    OrderBook book;
    book.load({});

    double out = 0.0;
    CHECK(book.sells().empty());
    CHECK_FALSE(book.bestSell(out));
    CHECK_FALSE(book.bestBuy(out));
}
