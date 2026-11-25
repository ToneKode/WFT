# wft

A C++20 library for reading public Warframe.Market data. No account and no token.

`Order::platinum` is the price of a bundle of `perTrade` units, not one unit.
`30` platinum with `perTrade` 6 is the same offer as `5` platinum with `perTrade` 1.
Compare with `Order::platinumPerUnit()` or `OrderBook`.

## What ships

- `MarketAPI` — items, versions, orders, top orders, recent orders, best sells
- `OrderBook` — one item's orders, ranked per unit
- `item_cache` — save and load the item catalogue, tagged with the versions hash
- `WfmSocket` — live new-order feed
- `src/main.cpp` — small demo: item count, then one item's best sell

## Reading

```cpp
MarketAPI api;
api.setContext("pc", "en", true);

MarketAPI::Versions versions = api.versions();
std::vector<Item> items;
std::string cachedHash;
try
{
    items = item_cache::loadItems("items.json", cachedHash);
}
catch (const std::runtime_error &)
{
}
if (items.empty() || cachedHash != versions.itemsHash)
{
    items = api.items();
    item_cache::saveItems("items.json", items, versions.itemsHash);
}

for (const auto &listing : api.bestSells({"khora_prime_set"}, 3))
{
    for (const auto &order : listing.sells)
    {
        std::cout << order.platinumPerUnit() << " plat each\n";
    }
}
```

`bestSells()` is one request per slug, paced at 3 requests per second. For Endo
and Ayatan star items, use `topOrdersForItem()`: the server ranks those, and
`OrderBook` does not copy that ranking.

Optional fields are `std::optional`, so "the server did not send this" never
collapses into a zero. That matters most on orders, where an absent `rank` and
a rank of `0` are different offers:

```cpp
const OrderBook book = api.bookForItem("khora_prime_set");
if (const std::optional<double> best = book.bestSell())
{
    std::cout << std::format("{} plat each\n", *best);
}

// rankLt wins over rank; leaving both unset sends no filter, and rank = 0 is
// a real filter rather than a sentinel.
const MarketAPI::TopOrders top =
    api.topOrdersForItem("serration", {.rank = 0});
```

## Live orders

`WfmSocket` subscribes with `@wfm|cmd/subscribe/newOrders` and parses
`@wfm|event/subscriptions/newOrder` into `Order`. Connect to
`wss://ws.warframe.market/socket` with subprotocol `wfm`.

You supply a `WsTransport`, or you own the socket and call `handle(frame)`.
Item and profile order subscriptions are not registered routes; calling
`subscribeItemOrders()` or `subscribeProfileOrders()` throws
`std::logic_error` without sending anything. Online count, status, and sign-in
are not this feed.

## Build

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

One preset for Windows, Linux, and macOS. It uses Ninja, takes its triplet
from the compiler in use (MinGW → `x64-mingw-dynamic`, MSVC → `x64-windows`,
Linux → `x64-linux`, macOS → `arm64-osx` / `x64-osx`), and installs
dependencies from `vcpkg.json` in manifest mode. `cmake --preset ci` is the
Release build with warnings as errors.

Needs CMake 3.23+, Ninja, a C++20 compiler, and vcpkg. The manifest asks for
`curl[websockets]` and `nlohmann_json`, so the WebSocket feature cannot be
forgotten. Configuration fails if the curl found was built without it.

Install vcpkg at `C:\Users\<you>\vcpkg` or set `VCPKG_ROOT`; it is picked up
automatically. A machine that already has curl and nlohmann_json can skip the
bootstrap with `-DWFT_BOOTSTRAP_VCPKG=OFF`.

Run the demo with `build/default/wft`. It calls the live API. On Windows the
curl DLL is copied next to the executable after link; on Linux and macOS the
shared system library is used.

Tests stay offline: `FakeTransport` for HTTP, `FakeWsTransport` for the socket
protocol.

## License

MIT. See `LICENSE`.
