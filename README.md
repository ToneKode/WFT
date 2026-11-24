# wft

A C++14 client library for the Warframe.Market API.

The API moved to v2 with a new response envelope, stricter order validation, and
a WebSocket. This library targets v2 for everything it implements, and keeps the
v1 session endpoint as the only login path because v2 authentication is
first-party only and public OAuth registration is not open yet.

## What This Repo Includes

- `include/wfm_api.h` — the API: auth, items, orders
- `include/http_client.h` — libcurl transport, rate limiting, retries
- `include/http_transport.h` — transport interface (the test seam)
- `include/order_validation.h` — per-item order request rules
- `include/wfm_json.h` — v2 payload to model mapping
- `include/api_error.h` — v2 error envelope parsing
- `include/constants.h` — base URLs and the in-memory item index
- `include/types.h` — request and response models

## Implemented Features

- Item data
  - Load every tradable item, with the metadata order validation needs
  - Substring search over slugs
  - Slug ↔ id resolution, O(1) both ways
- Authentication
  - Legacy v1 session sign-in (the only third-party path today)
  - Set or clear a token directly
- Orders
  - List your orders
  - Create buy or sell orders, validated against the item first
  - Partial updates, including rank, subtype, charges, and star counts
  - Partial or full close, returning the resulting transaction
  - Delete

## Order validation

The v2 API rejects order fields that do not apply to an item, and rejects
orders that omit fields the item requires. For example `perTrade` is required
for bulk-tradable items and forbidden otherwise, `rank` for ranked items,
`subtype` for items with subtypes, and star counts for Ayatan sculptures.

The library reads those rules from each item's own metadata and checks the
request before sending it, so a mistake surfaces as a local
`order_validation::ValidationError` naming the offending field instead of a
round trip and a terse error code.

```cpp
PlaceOrderRequest request;
request.itemSlug = "axi_a1_relic";
request.platinum = 10;
request.quantity = 6;
request.hasPerTrade = true;
request.perTrade = 6;      // required: relic trades in bulk
request.hasSubtype = true;
request.subtype = "intact"; // required: relic has subtypes

session.placeOrder(request); // throws locally if a rule is broken
```

## Requirements

- CMake 3.23+
- C++14 compiler
- libcurl
- nlohmann_json (3.10+)

## Build

```bash
cmake --preset mingw    # Windows / MinGW + vcpkg (toolchain auto-detected)
cmake --build --preset mingw
ctest --preset mingw
```

On Linux/macOS use `--preset linux`, which relies on system `libcurl` and
`nlohmann_json` instead of vcpkg.

### Windows setup

1. Install the MSYS2 UCRT64 toolchain so `g++` is on `PATH`.
2. Install dependencies:

   ```bash
   vcpkg install curl nlohmann-json --triplet x64-mingw-dynamic
   ```

3. Keep vcpkg at `C:\Users\<you>\vcpkg` (or set `VCPKG_ROOT`). The build
   finds it automatically; no toolchain flags needed.

If you prefer a bare `cmake -S . -B build`, pass
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` and
`-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic`.

### Linux/macOS setup

1. Install `libcurl` and `nlohmann_json` with your package manager.
2. Configure and build:

   ```bash
   cmake --preset linux
   cmake --build --preset linux
   ctest --preset linux
   ```

## Run

- Main demo: `build/mingw/wft`
- Basic example: `build/mingw/basic_usage`
- Order management example: `build/mingw/order_management`

Dynamic vcpkg triplets require the curl/nlohmann DLLs beside the executable. If
a binary fails to start, copy them from
`vcpkg/installed/x64-mingw-dynamic/bin` into the build directory.

## Testing

```bash
ctest --preset mingw
```

The suite runs fully offline. `tests/fake_transport.h` implements
`HttpTransport` with queued canned responses, so nothing touches the live API or
needs credentials, and a forgotten queue entry fails immediately instead of
hanging.

## Notes on the API transition

- v1 REST data endpoints are gone; v2 is the only data surface.
- v2 `/auth/signin` is first-party only and needs Firebase App Check. Public
  OAuth client registration is not open, so third-party integrations still use
  the v1 session endpoint.
- The WebSocket (`wss://ws.warframe.market/socket`, `wfm` subprotocol) is
  documented but not implemented here yet.
- The API is below 1.0 and documented as subject to breaking changes.
- The API rules require an identifiable `User-Agent` and ask clients to cache
  and prefer WebSocket subscriptions over polling. Requests here are
  rate-limited to 3 per second and retried with backoff on 429/509.

## Project Layout

- `include/wfm_api.h`: high-level API
- `include/types.h`: request and response models
- `include/http_client.h`: HTTP transport, rate limiting, retries
- `include/http_transport.h`: transport interface (test seam)
- `include/order_validation.h`: order request rules per item
- `include/wfm_json.h`: JSON to model mapping
- `include/api_error.h`: v2 error envelope
- `include/constants.h`: API constants and in-memory item index
- `src/main.cpp`: simple demo
- `tests/`: doctest suite (offline via `fake_transport.h`)
- `examples/basic_usage.cpp`: auth and item search flow
- `examples/order_management.cpp`: order workflow

## Contributing

See `AGENTS.md` for the tested build commands, project conventions, and the
non-obvious gotchas (global item database, auth state machine, MinGW triplet).

## License

MIT. See `LICENSE`.
