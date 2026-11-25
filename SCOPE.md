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
| `topOrdersForItem(slug, rank, rankLt)` | The server's own top 5 buy and sell |
| `recentOrders()` | Up to 500 orders from the last 4 hours |
| `bestSells(slugs, limit)` | Best listings across items, per unit |
| `setContext(platform, language, crossplay)` | Marketplace shard and localization |

### Ranking (`include/order_book.h`)

`OrderBook::load()` sorts one item's orders into a book.

The reason it exists: `Order::platinum` is the **total for a bundle of
`perTrade` units**, not a unit price. Live data shows `5 platinum / perTrade 1`
and `30 platinum / perTrade 6` as the same offer. Ranking on raw `platinum`
makes bulk sellers look several times too expensive. Every comparison goes
through `Order::platinumPerUnit()`.

It drops hidden orders and offline users, and keeps orders with no embedded
user, because `/orders/my` legitimately omits the owner.

### Catalogue cache (`include/item_cache.h`)

`saveItems()` and `loadItems()` write the item list to plain JSON in the API's
own field names, tagged with the collection hash. Loading throws on a missing,
unparsable, or empty file rather than returning an empty list that looks like a
working cache.

### Live orders (`include/wfm_socket.h`, `include/ws_client.h`)

`WfmSocket` speaks the WebSocket protocol: envelope routing, command id
correlation, `:ok`/`:error` handling, protection errors, and new-order dispatch.

`WsClient` is the libcurl `WsTransport`: one upgraded easy handle, caller-driven
`poll()` / `receive()`, no reader thread, no auto-reconnect. A dead socket
throws so `poll()` does not spin as idle; the caller reconnects and resends the
same subscribe filters.

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
- `ci` preset: Release with `-Werror`
- GitHub Actions on Linux and Windows
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

- 3,893 items load with correct metadata
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
passed 65 cases / 222 assertions. The Windows job has not been run on a Windows
runner.

Never run:

- The Windows CI job on a Windows runner
- MSVC triplet selection
- The demo executable against the live API

## Known gaps and risks

1. **Ninja must be on `PATH`.** Both presets need it, and it is not part of the
   documented Windows prerequisites.
2. **The WebSockets guard has a hole.** It reads `CURL_SUPPORTED_PROTOCOLS`,
   which vcpkg publishes. A system curl found in bare module mode publishes
   nothing, so the guard passes silently. Setting `WFT_BOOTSTRAP_VCPKG=OFF` is
   the path that bypasses it.
3. **Endo and star ranking.** The server ranks these differently. `OrderBook`
   does not replicate it; prefer `topOrdersForItem()` and note that
   `bestSells()` inherits the caveat.
4. **Below 1.0.** The API is documented as subject to breaking changes.

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
