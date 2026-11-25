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

`cmake --preset ci` is the local strict configuration: Release, with
`-DWFT_WARNINGS_AS_ERRORS=ON`. **Treat a warning as a failure.**

GitHub Actions does not use that preset. `.github/workflows/ci.yml` runs, on
both jobs:

```bash
cmake --preset default -DWFT_WARNINGS_AS_ERRORS=ON
cmake --build --preset default
ctest --preset default
```

That keeps the Debug `default` preset and turns warnings into errors. The
workflow starts on pull requests, on `workflow_dispatch`, and on pushes to
`main` and `dev/phase1`. The Linux job sets `CC=gcc` and `CXX=g++`.
`find_package` asks for nlohmann/json 3.10 or newer, CONFIG mode.

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
| `include/http_client.h` | libcurl `HttpTransport`; process-wide 350 ms pacing; retries 429, 509, and status >= 500 |
| `include/http_transport.h` | HTTP seam; `Get` only |
| `include/constants.h` | `V2_API`, `WS_API`, `WFT_USER_AGENT` |
| `include/types.h` | `Item`, `Order`, `UserShort` |
| `src/main.cpp` | Live demo: item count and one best sell |
| `tests/` | doctest; `fake_transport.h` and `fake_ws_transport.h` |

`wft_core` is an INTERFACE library. The demo and `wft_tests` link it, so tests
compile the same headers that ship. On Windows it also links `ws2_32`, because
`ws_client.h` calls `WSAPoll` and the test upgrade server calls the rest of
Winsock. The MinGW test target prepends the shared libcurl directory to `PATH`
so `ctest` can load that DLL.

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
  offline. An order whose type is neither `buy` nor `sell` is dropped. Equal
  unit prices keep the larger `quantity` first. `spread()` <= 0 means the
  book is crossed, usually a stale side.
- **`parseOrder` never fills `Order::itemSlug`.** Public payloads carry
  `itemId`. `ItemListings::itemSlug` is the slug the caller passed in. A
  missing `visible` becomes false, so both `OrderBook` and `onNewOrder` drop
  that order. A missing `perTrade`, or one that is not positive, counts as
  one unit inside `platinumPerUnit()`. `UserShort` is the exception to the
  optional-field rule: missing strings become empty and a missing
  `reputation` becomes 0.
- **Endo and star items rank differently server-side.** `OrderBook` does not
  replicate that. Prefer `MarketAPI::topOrdersForItem()`. `bestSells()` ranks
  through `OrderBook`, so the same caveat applies.
- **`bestSells()` costs one request per slug.** There is no multi-item orders
  endpoint. A 20-slug call is 20 requests, about 7 seconds, on purpose. The
  default `limit` is 5; `0` keeps every sell the book kept. Duplicate slugs
  are fetched twice. The first failure throws, so the caller gets nothing
  from the batch, including items already fetched. Leave the shared
  3-req/sec limiter in place; do not fan the calls out in parallel.
- **The limiter is one static clock, and `WsClient` does not use it.**
  `HttpClient::awaitSlot` keeps `nextAllowed` in a function-local static. The
  gap it adds is that instance's `minDelay_` (default 350 ms), so a test
  client constructed with a shorter delay moves the process-wide slot.
  `WsClient` never takes this lock.
- **Retries are 429, 509, and any status >= 500**, up to 4 attempts. The
  backoff is `500 * 2^attempt` ms plus a jitter in `[0, base/2]`. Any other
  non-2xx status throws `HttpStatusError` on the first response. The last
  throttled attempt throws too, body included.
- **Protocol tests use `FakeWsTransport`; `WsClient` tests do not dial
  `WS_API`.** `WfmSocket` sends `@wfm|cmd/subscribe/newOrders` (and the
  matching unsubscribe) and parses `@wfm|event/subscriptions/newOrder` into
  `Order`. The URL is `WS_API` with subprotocol `wfm`. Production code uses
  `WsClient`; multiplexed callers can still feed frames to `handle()`.
  `subscribeItemOrders()` / `subscribeProfileOrders()` throw
  `std::logic_error` because those routes are unregistered stubs on the server.
  Online count, status, and sign-in are not the order feed.
- **`subscribeNewOrders` returns as soon as the frame is sent.** The id is
  `wft-1`, `wft-2`, and so on. `:ok` arrives on a later `poll()` or `handle()`
  and increments `acknowledgedCommands()`. `:error` and `@wfm|protect/error`
  set `lastError()` to the payload when it is a string, otherwise
  `"unknown"`. Malformed frames increment `malformedFrames()` and do not
  throw. Unknown routes are ignored. An empty platform omits the `platform`
  field, which subscribes to every platform. Unsubscribe must resend the same
  filters. The server forces `crossplay` false when `platform` is `switch`;
  this client still sends the bool it was given.
- **`WsClient` frees the easy handle before the header list.**
  `destroyHandle()` resets the handle, then the `curl_slist`.
  Move-assignment calls `destroyHandle()` before taking the other object. The
  destructor does not send a close frame; `close()` sends `CURLWS_CLOSE` and
  then destroys the handle. A second `connect()` without `close()` throws.
  The handshake requires `CURLE_OK` and HTTP 101, uses a 10 second connect
  timeout, and sets no transfer timeout. The assembler caps a text message at
  1 MiB, discards binary frames, ignores ping and pong, and a close frame
  throws `WsClient: connection closed` after dropping the handle. The upgrade
  request sends `WFT_USER_AGENT`.
- **`WFT_USER_AGENT` is not the CMake version.** The wire identity is
  `wft/0.2.0 (+https://github.com/tonekode/wft)`. `project()` and
  `vcpkg.json` both say `1.0.0`. Change the User-Agent when the identity on
  the wire should change.
- **`setContext` stores the strings it is given.** Defaults are platform
  `pc`, language `en`, and crossplay true. Later requests send `Accept`,
  `User-Agent`, `Platform`, `Language`, and `Crossplay` (`true` or `false`).
  The platform argument is not checked against a list.
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
4. Public API changes are reflected in `README.md`, and in `SCOPE.md` when
   the in-or-out list changes.
