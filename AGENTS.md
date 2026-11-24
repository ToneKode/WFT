# AGENTS.md

Working notes for coding agents (and humans) in this repo. Follow these
commands verbatim; they are the tested path.

## What this is

A small C++14 client library for the Warframe.Market API. Header-heavy: most
logic lives in `include/*.h`, with one `.cpp` for the global item database.

There are two entry points, split by whether they need an account:

- `MarketAPI` (`market_api.h`) — anonymous, read-only market data. No token.
- `WarframeMarketAPI` (`wfm_api.h`) — auth and order placement.

Keep them separate. The read side works today; the write side depends on an
auth flow the API operator has not finished publishing.

## Commands

All commands run from the repo root.

```bash
# Configure (Windows / MinGW — vcpkg toolchain is auto-detected)
cmake --preset mingw

# Configure (Linux / macOS — system libcurl + nlohmann_json)
cmake --preset linux

# Build
cmake --build --preset mingw      # or: --preset linux

# Test
ctest --preset mingw              # or: --preset linux
```

`cmake --preset ci` is the strict configuration used by GitHub Actions:
Release build with `-Werror`. **Treat a warning as a failure**; the CI job does.

### Windows prerequisites

- MSYS2 UCRT64 toolchain (`g++` on `PATH`)
- vcpkg at `C:\Users\<you>\vcpkg` (or set `VCPKG_ROOT`), with
  `vcpkg install curl nlohmann-json --triplet x64-mingw-dynamic`

The top of `CMakeLists.txt` finds vcpkg automatically. If you configure with a
plain `cmake -S . -B build`, you must pass
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` and
`-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic`. The MSVC-runtime default triplet
fails at link time with an error that never mentions triplets — see
`cmake/WftDependencies.cmake`.

## Layout

| Path | Role |
| --- | --- |
| `include/market_api.h` | **Read-only API (no auth).** `MarketAPI` — items, versions, orders, top, recent, best sells |
| `include/wfm_api.h` | **Write API.** `WarframeMarketAPI` — auth, item DB load, orders |
| `include/order_book.h` | `OrderBook` — unit-normalised ranking of one item's orders |
| `include/item_cache.h` | `item_cache::saveItems`/`loadItems` — catalogue on disk, tagged with the version hash |
| `include/wfm_socket.h` | `WfmSocket` — WebSocket protocol layer (routes, dispatch) |
| `include/ws_transport.h` | Transport seam for `WfmSocket`, so protocol is testable |
| `include/order_validation.h` | Per-item order rules; `ValidationError` names the bad field |
| `include/wfm_json.h` | v2 JSON → model mapping (`wfm_json::parseOrder`, etc.) |
| `include/api_error.h` | v2 error envelope parsing; `ApiError` carries `request`/`inputs` codes |
| `include/wfm_client.h` | Legacy interactive console demo over `WarframeMarketAPI` |
| `include/http_client.h` | libcurl implementation of `HttpTransport`; 350 ms pacing, 429/509 retry |
| `include/http_transport.h` | Transport interface; the seam that makes tests possible |
| `include/constants.h` | `V1_API` / `V2_API` / `WS_API`, `WFT_USER_AGENT`, global `itemDB` |
| `include/types.h` | Request/response models |
| `src/constants.cpp` | Defines the global `itemDB` |
| `tests/` | doctest suite; `tests/fake_transport.h` and `fake_ws_transport.h` are the fakes |
| `examples/` | Runnable demos; built as separate executables |

`wft_core` is the library target. Executables and tests link it, so tests
exercise the same code that ships.

## Things that will bite you

- **`itemDB` is a process-wide global.** Tests that touch it call
 `itemDB.clear()` first (see `tests/test_api.cpp`). Order-dependent tests
 will pass locally and fail in isolation — always reset.
- **`loadItems()` must run before orders, not just lookups.** Order request
 validation reads each item's rank/subtype/bulk metadata, so placing an order
 for an unloaded slug throws immediately rather than guessing the shape.
- **`loadItems()` is all-or-nothing.** It parses into a scratch `ItemDatabase`
 and swaps on success, so a failed reload cannot corrupt a working index.
- **Never call the live API in a test.** Construct
 `WarframeMarketAPI(std::unique_ptr<HttpTransport>(new FakeTransport()))` and
 queue replies. `FakeTransport` throws immediately if a test forgets to queue
 a reply, so a leaked network call fails fast instead of hanging.
- **`FakeTransport` replies are FIFO**, one per request. A test that makes N
 requests queues N replies.
- **Header maps are case-insensitive** (`CaseInsensitiveLess`). A
 case-sensitive lookup misses a token the server spelled `Authorization`;
 that bug silently produces an empty token and a confusing JWT error later.
 Keep `HttpTransport::Headers` as the map type when adding methods.
- **Errors live in the envelope, not the status code.** Use
 `throwIfApiError()` and check both the nested (`error.request`) and flat
 (`request`) shapes — the docs overview and live traffic disagree, so both
 must work. `ApiError` carries `requestCodes()` and `inputCodes()`
 (`"platinum=app.field.tooSmall"`).
- **`HttpStatusError` carries the body.** The useful error code is usually in
 the body of a 4xx, so unwrap it before reporting a bare status.
- **Auth is a state machine** (`UnauthenticatedState`/`AuthenticatedState`).
 Order methods go through `api.authenticated()`, which throws unless
 `setJWT()` set a non-empty token.
- **`setJWT("")` clears auth.** Use `clearJWT()` for intent.
- **Tokens are accepted with or without the legacy `"JWT "` prefix**;
 requests always send `Bearer <token>`. Do not reintroduce
 `extractBearerToken()`'s old throw-on-unprefixed behaviour.
- **`login()` targets v1 on purpose.** v2 `/auth/signin` is first-party only
 and needs Firebase App Check; public OAuth registration is not open. Keep
 login behind one method so the eventual OAuth swap is a single change.
- **`http_client.h` and `wfm_client.h` define `using json = nlohmann::json;`
 at global scope.** Including both in one translation unit is fine (identical
 alias), but do not introduce a different `json` alias.
- **`Order::platinum` is a BUNDLE TOTAL, not a unit price.** This is the
 single most expensive thing to get wrong. `platinum` is the price for
 `perTrade` units. Live example: buy orders on one relic appear as
 `5 plat / perTrade 1` and `30 plat / perTrade 6` — the same offer. Always
 compare via `Order::platinumPerUnit()` or let `OrderBook` do it. Ranking on
 raw `platinum` makes every bulk seller look several times too expensive.
- **`OrderBook` is the only ranking path.** It drops hidden orders and
 offline users (who cannot trade) but keeps orders with no embedded `user`,
 because `/orders/my` legitimately omits the owner. `spread()` returning <= 0
 means the book is crossed, which usually means a stale side.
- **Endo and star items rank differently server-side.** `OrderBook` does not
 replicate that. For Ayatan sculptures and Endo listings prefer
 `MarketAPI::topOrdersForItem()`, which returns the server's own ranking. The
 same caveat applies to `bestSells()`, which ranks through `OrderBook`.
- **`bestSells()` costs one request per slug.** It is a loop over
 `bookForItem()` relying on the transport's shared pacing, not a batcher: there
 is no multi-item orders endpoint, so a 20-slug call is a 20-request, ~7-second
 call by design. Do not "optimise" it into parallel requests; the 3-req/sec
 limit is per client and shared.
- **No WebSocket transport ships, deliberately.** `WfmSocket` is the tested
 protocol layer, but there is no `WsClient`: the vcpkg `curl` port is built
 without WebSocket support (`CURL_VERSION_WEBSOCKETS` unset, no `ws`/`wss` in
 `curl_version_info().protocols`). Verified at runtime. Do not add a libcurl
 WS transport and assume it works — check that bit first. `WfmSocket::handle()`
 exists so frames from a WS-capable client you supply can be fed in, and is the
 intended path for a caller that already owns a socket; `WsTransport` is for
 callers that want this library to own the read loop.
- **`MarketAPI::versions()` is the cache key, not decoration.** It returns the
 `collections.items` hash and `updatedAt`. A caller compares the hash saved with
 a cached catalogue against the current one and re-fetches only on change.
 Returning an empty `itemsHash` on an unexpected payload is deliberate: the
 caller then refreshes, which is always safe.
- **`item_cache::loadItems()` throws rather than returning empty.** A missing,
 unparsable, or empty cache is an error, because an empty item list looks like a
 working cache and would leave order placement validating against nothing.
 Entries go through the same `parseItem` as live traffic, and `toItemJson()`
 writes the API's own field names so the two cannot drift.
- **`FakeTransport` throws `HttpStatusError` on non-2xx**, matching
 `HttpClient`. Keep that contract: a fake that returned error bodies as normal
 responses would leave the 4xx error-body path untested.
- **libcurl global init is process-wide** and must not move back into the
 client's ctor/dtor: a second client's handles die with the first destructor.
- **GCC `-Wdangling-reference` fires on helpers returning a reference while
 taking a `const std::string&`.** `requireDataArray`/`requireDataObject`
 return pointers and take `const char*` to stay clean under `-Werror`.

## Conventions

- C++14. No `std::optional` in the library headers (it appears in the legacy
 `wfm_client.h`); the request structs use `hasX` flags instead.
- 4-space indent, braces on the same line, 100-column limit (`.clang-format`).
- Match the existing doc-comment style (`@brief`, `@param`, `@return`) on
 public API.
- Prefer fixing a warning over suppressing it.
- Keep `WarframeMarketAPI` about transport, auth state, and endpoint wiring.
 JSON mapping belongs in `wfm_json.h`, request rules in `order_validation.h`,
 and envelope handling in `api_error.h`.
- Put new anonymous reads on `MarketAPI`, not `WarframeMarketAPI`. If a method
 needs no token, it does not belong on the class that owns auth.
- Prefer the smallest change that works. No abstraction, option, or "for
 later" scaffolding nobody asked for; note what you skipped in one line.

## Definition of done

1. `cmake --build --preset mingw` (or `linux`) completes with no warnings.
2. `ctest --preset mingw` passes.
3. New behaviour has a test in `tests/` that runs offline via `FakeTransport`
 or `FakeWsTransport`.
4. Public API changes are reflected in `README.md`.
