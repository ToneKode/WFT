# AGENTS.md

Guidance for coding agents working in this repository.

wft is a small C++ client for the public Warframe.Market HTTP API. Almost all of the library lives in headers. One translation unit owns a process-wide item index. Three executables demonstrate the client. The repo has no test suite and no CI config.

## Supported code

The supported client is `WarframeMarketAPI` in `include/wfm_api.h`. `src/main.cpp`, `examples/basic_usage.cpp`, and `examples/order_management.cpp` all use it. New API surface goes on this class.

`include/wfm_client.h` (`WfmClient`) and `include/user.h` (`User`) are an older interactive client. `WfmClient` prints to stdout, reads from stdin inside library methods, and uses `std::optional`, so that header compiles as C++17 and fails as C++14. Its `login` takes a required `device_id` argument. Its order methods strip the token with `jwt.substr(4)` and its item load sends User-Agent `wft/1.0`. `CMakeLists.txt` does not build either header. Leave them in place unless the task is specifically about that older path.

`include/types.h` defines `Item` and `Order` for a statistics feature that is commented out in `WfmClient::getItemStats` and `User::populateInventory`. Live order code uses `OrderDetail`, `SellOrderRequest`, and `UpdateOrderRequest`.

## Build

The project requires C++14 (`CMAKE_CXX_STANDARD 14`), CMake 3.15 or newer, libcurl, and nlohmann/json 3.

```
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
```

Debian and Ubuntu packages that satisfy those modules:

- `build-essential`
- `cmake`
- `libcurl4-openssl-dev`
- `nlohmann-json3-dev` (provides `/usr/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake`)

```
cmake -S . -B build
cmake --build build
```

Single-config generators write the binaries to `build/`:

| Target | Source | What it does |
| --- | --- | --- |
| `wft` | `src/main.cpp` | Loads the public item list and searches the slug substring `prime`. No login. |
| `basic_usage` | `examples/basic_usage.cpp` | Prompts for email and password, logs in, loads items, searches, and round-trips a slug and id. |
| `order_management` | `examples/order_management.cpp` | Menu that lists, creates, updates, closes, and deletes live orders on the signed-in account. |

`src/constants.cpp` is the only library `.cpp`. Every executable links it because it defines the global `itemDB`. A program that calls `loadItems()` links this file once. Linking it from two static libraries into one binary is an ODR violation.

Windows builds use vcpkg packages `curl` and `nlohmann-json` and pass `CMAKE_TOOLCHAIN_FILE`. Multi-config generators place binaries under `build/Debug` or `build/Release`.

There are no unit tests. `cmake --build build` is the in-repo check. `wft` needs network access. The two examples need a real Warframe.Market account, and `order_management` changes live orders.

## Layout

```
include/wfm_api.h       supported client
include/http_client.h   libcurl wrapper and per-client 350ms spacing
include/types.h         request and response structs
include/constants.h     base URLs and ItemDatabase
src/constants.cpp       definition of global itemDB
src/main.cpp            unauthenticated demo
examples/               interactive demos of WarframeMarketAPI
include/wfm_client.h    older interactive client
include/user.h          thin holder around WfmClient
```

`cmake --install` copies the three executables to `bin` and the headers to `include/wft`.

## Behavior to preserve

Base URLs live in `include/constants.h`.

- Sign-in is `POST https://api.warframe.market/v1/auth/signin`.
- Items and orders use `https://api.warframe.market/v2`.

### Authentication

`login(email, password, device_id = "")` posts JSON fields `auth_type`, `email`, `password`, and `device_id`. The request headers are `Content-Type: application/json` and `Authorization: JWT`. The token is taken from the response header map key `authorization`, which is the exact string stored by `HttpClient`'s header callback. User id and in-game name come from `payload.user.id` and `payload.user.ingame_name`. `login` returns an `AuthResponse` and does not store the token.

The caller passes `auth.token` to `setJWT`. A non-empty string marks the client authenticated. `setJWT("")` and `clearJWT()` mark it unauthenticated. `isAuthenticated()` is true for any non-empty string. The `JWT ` prefix is checked on the first authenticated call, not in `setJWT`.

`authenticated()` throws `std::runtime_error` until `setJWT` has been called with a non-empty token, then returns `AuthenticatedClient`. Order methods exist only on that nested type: `getMyOrders`, `createSellOrder`, `updateOrder`, `closeOrder`, and `deleteOrder`.

`extractBearerToken()` requires the stored token to begin with the characters `JWT` followed by a space. Authenticated v2 calls send `Authorization: Bearer ` plus the remainder. A token without that prefix throws `Invalid JWT format`.

### Items

`loadItems()` GETs `/v2/items` and writes the global `itemDB`.

- `idToSlug` and `slugToId` overwrite when the same key arrives again.
- `allSlugs` is appended and is never cleared.

A second `loadItems()` duplicates every slug in `allSlugs`. `size()`, `getItemCount()`, and `searchItems()` then grow even though the maps stay unique. Call `loadItems()` once per process, or clear `itemDB` before calling it again.

`searchItems` is a case-sensitive `std::string::find` over slugs. It does not search display names.

`getIDBySlug` and `getSlugByID` throw `std::runtime_error` when the key is missing. `createSellOrder` resolves the slug with `getIDBySlug`. `getMyOrders` resolves every `itemId` with `getSlugByID`. Both calls need a prior successful `loadItems()`.

Every `WarframeMarketAPI` and every `WfmClient` share the one global `itemDB`.

### Orders

Creating a sell order posts to `/v2/order/` (trailing slash) with `itemId`, `type` set to `sell`, `platinum`, `quantity`, `visible`, and `rank` when `SellOrderRequest::hasRank` is set. The returned value is `data.id`. The convenience overload `createSellOrder(slug, platinum, quantity, visible)` does not send a rank.

Updating an order PATCHes `/v2/order/{id}` with only the fields whose `has*` flags are set on `UpdateOrderRequest`. Those flags keep the public types valid C++14. An update with no flags throws `std::invalid_argument`.

Closing an order POSTs `/v2/order/{id}/close` with `{"quantity": n}`.

Deleting an order sends `DELETE /v2/order/{id}`. `HttpClient::Delete` returns the status code and does not throw on an HTTP error status. `deleteOrder` throws unless that code is exactly 200.

Create, update, and close throw `std::runtime_error` when the JSON `error` field is non-null. `post` and `patch` already throw on any status outside 200–299 and include the response body in the message.

The client can list buy and sell orders (`OrderDetail::orderType`). It can create sell orders only.

### HTTP client

Each `HttpClient` waits so that at least 350ms pass between its own requests. Instances do not share that clock.

`post` and `patch` throw on curl failures and on HTTP statuses outside 200–299. `Get` returns the body and does not check the status. `Delete` returns the status code.

Each `HttpClient` calls `curl_global_init` in its constructor and `curl_global_cleanup` in its destructor. The copy constructor is implicit and does not call `curl_global_init`, so copying an instance and then destroying both objects unbalances curl's init count. Treat `HttpClient`, `WarframeMarketAPI`, and `WfmClient` as non-copyable. `AuthenticatedClient` holds a `WarframeMarketAPI&` and stays within the lifetime of that object.

`using json = nlohmann::json;` in the headers is visible to every translation unit that includes them.

## Conventions

- Keep public request types valid C++14. Optional fields use explicit `has*` flags, as `UpdateOrderRequest` does.
- Keep order operations on `AuthenticatedClient`.
- Throw `std::runtime_error` for API and HTTP failures and `std::invalid_argument` for caller mistakes.
- Keep the item index in the global `itemDB` defined in `src/constants.cpp`.
- The supported client's User-Agent is `wf-market-cpp/0.1.0`. Authenticated v2 calls also send `Platform: pc`, `Language: en`, and `Crossplay: true`.

## Documentation

- `README.md` is the user-facing build and usage guide.
- `examples/README.md` describes the two interactive programs.
- A behavior change in `wfm_api.h`, `http_client.h`, or `constants.h` updates this file and `README.md` in the same change.

## Leave for a task that asks for it

- Restoring `getItemStats` or `User::populateInventory`.
- Adding a test framework while changing something else.
- Moving sign-in off v1 or items and orders off v2.
