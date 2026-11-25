# Scope

What this project is, what it deliberately is not, and what is still open.
Written against the tree on `dev/phase1`.

## In one sentence

A header-only C++20 library that reads public Warframe.Market data — items,
orders, prices, and the live new-order feed — with no account and no token.

## What ships

### Market reads (`include/market_api.h`)

`MarketAPI` speaks the v2 HTTP API anonymously.

| Method | Purpose |
| --- | --- |
| `items()` | Every tradable item, with the metadata downstream code needs |
| `versions()` | `collections.items` hash and `updatedAt`, the cache key |
| `item(slug)` | One item |
| `ordersForItem(slug)` | Visible orders for one item |
| `bookForItem(slug)` | The same, indexed into an `OrderBook` |
| `topOrdersForItem(slug, filter)` | The server's own top buy and sell. `RankFilter::rankLt` wins over `rank`. No filter sends no query, and `rank = 0` is a real filter |
| `recentOrders()` | Up to 500 orders from the last 4 hours |
| `bestSells(slugs, limit)` | Best listings across items, per unit. Default limit 5; `0` keeps every sell the book kept. One request per slug, duplicates included. The first failure throws, so the caller gets no partial batch |
| `setContext(platform, language, crossplay)` | Marketplace shard and localization. Defaults are `pc`, `en`, crossplay on. The strings are sent as given |

### Ranking (`include/order_book.h`)

`OrderBook::load()` sorts one item's orders into a book.

The reason it exists: `Order::platinum` is the **total for a bundle of
`perTrade` units**, not a unit price. Live data shows `5 platinum / perTrade 1`
and `30 platinum / perTrade 6` as the same offer. Ranking on raw `platinum`
makes bulk sellers look several times too expensive. Every comparison goes
through `Order::platinumPerUnit()`.

It drops hidden orders and offline users, and keeps orders with no embedded
user, because an unknown owner is not offline. A missing `visible` is parsed
as false, so that order is dropped. Orders whose type is neither `buy` nor
`sell` are dropped. Equal unit prices keep the larger quantity first.

`parseOrder` leaves `Order::itemSlug` empty. Public order payloads carry
`itemId`. `bestSells` records the requested slug on `ItemListings`, not on
each `Order`.

### Catalogue cache (`include/item_cache.h`)

`saveItems()` and `loadItems()` write the item list to plain JSON in the API's
own field names, tagged with the collection hash. Loading throws on a missing,
unparsable, or empty file rather than returning an empty list that looks like a
working cache.

### Live orders (`include/wfm_socket.h`, `include/ws_client.h`)

`WfmSocket` speaks the WebSocket protocol: envelope routing, command id
correlation, `:ok`/`:error` handling, protection errors, and new-order dispatch.

`WsClient` is the libcurl `WsTransport`: one upgraded easy handle, caller-driven
`poll()` / `receive()`, no reader thread, no auto-reconnect, and no share of
the HTTP rate limiter. A dead socket throws so `poll()` does not spin as idle;
the caller reconnects and resends the same subscribe filters. `close()` sends
a WebSocket close frame. Destroying the client does not. The `curl_slist` is
freed only after `curl_easy_cleanup`.

`subscribeNewOrders` returns a `wft-N` command id as soon as the frame is
written. `:ok` and `:error` arrive later through `poll()` or `handle()`. An
empty platform omits the field and subscribes to every platform. Unsubscribe
must repeat the same filters.

Active and wired:

- `@wfm|cmd/subscribe/newOrders`
- `@wfm|cmd/unsubscribe/newOrders`
- `@wfm|event/subscriptions/newOrder`

Stubbed to throw `std::logic_error` without touching the transport, because the
server has not registered those routes:

- `subscribeItemOrders()`
- `subscribeProfileOrders()`

### Infrastructure

- `vcpkg.json` manifest pinning `curl[websockets]` and `nlohmann_json`
- Shared `curl_runtime.h` process-wide init (never cleaned up)
- One `default` CMake preset (Ninja), triplet chosen from the compiler
- `ci` preset: Release with warnings as errors (`-Werror`, or `/WX` on MSVC)
- GitHub Actions workflow for Linux and Windows. Jobs start on pull requests,
  `workflow_dispatch`, and pushes to `main` and `dev/phase1`. Each job
  configures with `cmake --preset default -DWFT_WARNINGS_AS_ERRORS=ON`
  (Debug, warnings as errors). The `ci` preset is the local Release
  equivalent and is not what Actions runs. The Linux job pins `CC=gcc` and
  `CXX=g++`
- `WFT_USER_AGENT` is `wft/0.2.0 (+https://github.com/tonekode/wft)`. The
  CMake project version and the vcpkg `version-string` are both `1.0.0`
- Offline doctest cases; no test dials `WS_API` or the live HTTP API

## What is deliberately not here

| Excluded | Why |
| --- | --- |
| Sign-in, order create/update/close/delete | Not market reading. Removed with the read-only split. |
| Console demo menus | An application, not a library. |
| Process-wide item index | Existed only for order placement. `MarketAPI::items()` covers reads. |
| Item and profile subscriptions | Documented as unregistered backend stubs. Sending them would be pretending. |
| Online count, status, activity, account patch | Presence and account traffic, not the order book. |
| OAuth 2.0 | Not publicly available; third-party client registration is closed. |
| Contracts, rivens, liches, sisters, missions, NPCs, locations | Documented endpoints, not implemented. |
| Achievements, dashboard, groups, user profiles | Same. |
| Batch multi-item orders | No such endpoint exists. `bestSells()` is one request per slug. |
| Thread pool or scheduler | The shared 3-req/sec limiter is sufficient. |
| Auto-reconnect / reader thread | The caller owns reconnect and the `poll()` loop. |

## What is verified

Checked against the live API during development:

- 3,893 items loaded with correct metadata during development. That count is a
  snapshot, not a contract; `items()` returns whatever the catalogue contains
- Cache round-trip preserves every field order validation reads, and the hash
- Per-unit pricing semantics, confirmed from real order data
- `ordersForItem`, `topOrdersForItem`, `recentOrders`, `bookForItem`
- All six read endpoints return 200

Verified only against fakes / offline transport tests:

- Everything in `WfmSocket` (protocol on `FakeWsTransport`)
- `WsClient` assembler and refused-connection behaviour (no dial to `WS_API`)

The Linux CI commands were run locally with gcc, the vcpkg manifest, and
`cmake --preset default -DWFT_WARNINGS_AS_ERRORS=ON`. Configure found curl with
`WS` and `WSS`. The build was clean under `-Werror`, and `ctest --preset default`
passed 65 cases / 222 assertions. The GitHub Actions Linux job passed the same
way. The Windows job uses MSYS2 UCRT64. It first failed to link `wft_tests` on
`WSAPoll` and the test's Winsock calls. `wft_core` now links `ws2_32`, and the
following Windows job passed configure, build, and `ctest`.

Never run:

- MSVC triplet selection
- The demo executable against the live API

## Known gaps and risks

1. **Ninja is a separate install on Windows.** Both presets need it on `PATH`.
   MSYS2 does not ship it with the compiler. The Windows Actions job installs
   `mingw-w64-ucrt-x86_64-ninja`; a local UCRT64 shell needs the same package.
2. **The WebSockets guard has a hole.** It reads `CURL_SUPPORTED_PROTOCOLS`,
   which vcpkg publishes. A system curl found in bare module mode publishes
   nothing, so the guard passes silently. Setting `WFT_BOOTSTRAP_VCPKG=OFF` is
   the path that bypasses it.
3. **Endo and star ranking.** The server ranks these differently. `OrderBook`
   does not replicate it; prefer `topOrdersForItem()` and note that
   `bestSells()` inherits the caveat.
4. **Warframe.Market's HTTP API is below 1.0** and is documented as subject to
   breaking changes. That is separate from this repo's CMake/vcpkg version
   string (`1.0.0`) and from the User-Agent (`wft/0.2.0`).

## Constraints we do not control

- **Rules.** Warframe.Market asks every client to identify itself, caches
  aggressively, and prefers subscriptions to polling. Trade bots are described
  as a grey area with stricter limits expected. This library reads; it does not
  automate trading.
- **Auth.** v2 `/auth/signin` is first-party only with Firebase App Check.
  Public OAuth registration is closed. Any write path is blocked on that.
- **Rate limit.** 3 requests per second, enforced by the shared limiter.

## Next step, if any

Verify one live `pc` newOrders subscription end-to-end (`:ok` plus one
new-order callback) in an environment that may dial `WS_API`. Item and profile
routes stay blocked until the server registers them.
