# AGENTS.md

Working notes for coding agents (and humans) in this repo. Follow these
commands verbatim; they are the tested path.

## What this is

A small C++20 library for reading public Warframe.Market data. Header-only.
No account and no token.

- `MarketAPI` (`market_api.h`) — items, versions, orders, top, recent, best sells.
- `OrderBook` (`order_book.h`) — unit-normalised ranking of one item's orders.
- `item_cache` (`item_cache.h`) — catalogue on disk, tagged with the version hash.
- `WfmSocket` (`wfm_socket.h`) — live new-order feed over `WsClient` or any `WsTransport`.

See `SCOPE.md` for what is in, what is out, and what is still open.

## Commands

All commands run from the repo root.

```bash
# Configure (any OS — Ninja + the vcpkg manifest; the triplet follows the compiler)
cmake --preset default

# Build
cmake --build --preset default

# Test
ctest --preset default
```

`cmake --preset ci` is the strict configuration: Release build with `-Werror`.
**Treat a warning as a failure**; both CI jobs do, and they configure with
`-DWFT_WARNINGS_AS_ERRORS=ON`.

### Prerequisites

- CMake 3.23+, **Ninja on `PATH`**, and a C++20 compiler
- vcpkg at `C:\Users\<you>\vcpkg` (or set `VCPKG_ROOT`)
- Windows: the MSYS2 UCRT64 toolchain, so `g++` is on `PATH`

Dependencies come from `vcpkg.json` in manifest mode, which requires
`curl[websockets]` and `nlohmann_json`. There is no separate `vcpkg install`
step, so WebSockets cannot be left out. The top of `CMakeLists.txt` finds vcpkg
automatically, and configuration fails if the curl found has no WebSockets.

Set `-DWFT_BOOTSTRAP_VCPKG=OFF` only when the machine already provides curl and
nlohmann_json.

## Layout

| Path | Role |
| --- | --- |
| `include/market_api.h` | `MarketAPI` — anonymous reads |
| `include/order_book.h` | `OrderBook` — per-unit ranking |
| `include/item_cache.h` | `item_cache::saveItems` / `loadItems` |
| `include/wfm_socket.h` | `WfmSocket` — new-order protocol |
| `include/ws_client.h` | libcurl `WsTransport`; no reader thread, no auto-reconnect |
| `include/ws_transport.h` | Socket seam so the protocol is testable |
| `include/curl_runtime.h` | Process-wide `curl_global_init`; never cleaned up |
| `include/wfm_json.h` | v2 JSON → `Item` / `Order` / `UserShort` |
| `include/api_error.h` | v2 error envelope; `ApiError` carries `request` / `inputs` codes |
| `include/http_client.h` | libcurl `HttpTransport`; 350 ms pacing, 429/509 retry |
| `include/http_transport.h` | HTTP seam; `Get` only |
| `include/constants.h` | `V2_API`, `WS_API`, `WFT_USER_AGENT` |
| `include/types.h` | `Item`, `Order`, `UserShort` |
| `src/main.cpp` | Live demo: item count and one best sell |
| `tests/` | doctest; `fake_transport.h` and `fake_ws_transport.h` |

`wft_core` is an INTERFACE library. The demo and `wft_tests` link it, so tests
compile the same headers that ship.

## Things that will bite you

- **Never call the live API in a test.** Construct
  `MarketAPI(std::make_unique<FakeTransport>())` or
  `WfmSocket` over `FakeWsTransport`, and queue replies. `FakeTransport` throws
  immediately if a test forgets to queue a reply.
- **`FakeTransport` replies are FIFO**, one per request. A test that makes N
  requests queues N replies. Non-2xx replies throw `HttpStatusError` with the
  body, matching `HttpClient`.
- **Header maps are case-insensitive** (`CaseInsensitiveLess`). Keep
  `HttpTransport::Headers` as the map type when adding methods.
- **Errors live in the envelope, not the status code.** Use
  `throwIfApiError()` and check both the nested (`error.request`) and flat
  (`request`) shapes — the docs overview and live traffic disagree, so both
  must work. `ApiError` carries `requestCodes()` and `inputCodes()`.
- **`HttpStatusError` carries the body.** Unwrap a 4xx body before reporting a
  bare status.
- **`http_client.h` defines `using json = nlohmann::json;` at global scope.**
  Do not introduce a different `json` alias. `MarketAPI` relies on that one.
- **Ninja is required and is not in MSYS2 by default.** Both presets use it.
  If configure fails with "CMAKE_MAKE_PROGRAM not set", install Ninja or put it
  on `PATH`; vcpkg may already have one under
  `vcpkg/downloads/tools/ninja-*-windows/ninja.exe`.
- **The WebSockets guard can pass silently.** `cmake/WftDependencies.cmake`
  checks `CURL_SUPPORTED_PROTOCOLS`, which vcpkg's config publishes. A system
  curl found in bare module mode publishes nothing, so the check passes even if
  that curl lacks WebSockets. The `WFT_BOOTSTRAP_VCPKG=OFF` escape hatch is
  exactly that path.
- **`order_book.h` is the only ranking path, and it is load-bearing.**
  `Order::platinum` is a bundle total for `perTrade` units, not a unit price.
  Live example: `5 plat / perTrade 1` and `30 plat / perTrade 6` are the same
  offer. Compare with `Order::platinumPerUnit()` or let `OrderBook` do it.
  Ranking on raw `platinum` makes every bulk seller look several times too
  expensive.
- **`OrderBook` drops hidden orders and offline users** (`UserShort::status`
  is parsed so this works). Orders with no embedded user stay: unknown is not
  offline. `spread()` <= 0 means the book is crossed, usually a stale side.
- **Endo and star items rank differently server-side.** `OrderBook` does not
  replicate that. Prefer `MarketAPI::topOrdersForItem()`. `bestSells()` ranks
  through `OrderBook`, so the same caveat applies.
- **`bestSells()` costs one request per slug.** There is no multi-item orders
  endpoint. A 20-slug call is 20 requests, about 7 seconds, on purpose. Leave
  the shared 3-req/sec limiter in place; do not fan the calls out in parallel.
- **Protocol tests use `FakeWsTransport`; `WsClient` tests do not dial
  `WS_API`.** `WfmSocket` sends `@wfm|cmd/subscribe/newOrders` (and the
  matching unsubscribe) and parses `@wfm|event/subscriptions/newOrder` into
  `Order`. The URL is `WS_API` with subprotocol `wfm`. Production code uses
  `WsClient`; multiplexed callers can still feed frames to `handle()`.
  `subscribeItemOrders()` / `subscribeProfileOrders()` throw
  `std::logic_error` because those routes are unregistered stubs on the server.
  Online count, status, and sign-in are not the order feed.
- **`MarketAPI::versions()` is the cache key.** It returns the
  `collections.items` hash and `updatedAt`. An empty `itemsHash` on an
  unexpected payload is deliberate: the caller refreshes, which is safe.
- **`item_cache::loadItems()` throws rather than returning empty.** A missing,
  unparsable, or empty cache is an error, because an empty list looks like a
  working cache. Entries go through `parseItem`, and `toItemJson()` writes the
  API's own field names.
- **libcurl global init is process-wide** and stays out of the client
  ctor/dtor: a second client's handles die with the first destructor.
- **GCC `-Wdangling-reference` fires on helpers returning a reference while
  taking a `const std::string&`.** `requireDataArray` / `requireDataObject`
  return references but take their context as `std::string_view`, which GCC 13
  and 14 both accept under `-Werror`. Keep it that way rather than reintroducing
  `const std::string&` or a pointer return.
- **Absent is not zero.** Optional fields are `std::optional`; use
  `optionalValue()` / `writeOptional()` in `wfm_json.h` rather than defaults.
  `RankFilter` expresses "no filter" as `std::nullopt`, so `rank = 0` is a real
  filter and not the old `-1` sentinel.

## Conventions

- C++20. Optional fields are `std::optional`; there are no `hasX` flags.
- 4-space indent, braces on the same line, 100-column limit (`.clang-format`).
- Match the existing doc-comment style (`@brief`, `@param`, `@return`) on
  public API.
- Prefer fixing a warning over suppressing it.
- HTTP reads go on `MarketAPI`. Live orders go through `WfmSocket`. JSON
  mapping belongs in `wfm_json.h`. Envelope handling belongs in `api_error.h`.
- Prefer the smallest change that works. No abstraction, option, or "for
  later" scaffolding nobody asked for; note what you skipped in one line.

## Definition of done

1. `cmake --build --preset default` completes with no warnings.
2. `ctest --preset default` passes.
3. New behaviour has a test in `tests/` that runs offline via `FakeTransport`
   or `FakeWsTransport`.
4. Public API changes are reflected in `README.md`.
