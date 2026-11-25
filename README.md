# wft

Small C++ client for the [Warframe.Market](https://warframe.market) HTTP API.

The supported entry point is `WarframeMarketAPI` in `include/wfm_api.h`. Sign-in uses API v1. Item and order calls use API v2. The library is header-only except for `src/constants.cpp`, which defines the process-wide item index.

## What you can do

- Sign in with email and password, then store or clear the returned JWT.
- Load the tradable item list, search slugs by substring, and convert between item id and slug.
- After `setJWT`, list your orders, create a sell order, and update, close, or delete an order by id through `WarframeMarketAPI::authenticated()`.

`include/wfm_client.h` and `include/user.h` are an older interactive client. The examples and `src/main.cpp` use `WarframeMarketAPI`.

## Requirements

- CMake 3.15 or newer
- A C++14 compiler
- libcurl
- nlohmann/json 3, with a CMake config package named `nlohmann_json`

Debian and Ubuntu:

```bash
sudo apt install build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

Windows with vcpkg:

```bash
vcpkg install curl nlohmann-json
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

On Windows, pass the vcpkg toolchain file at configure time:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

With a dynamic vcpkg triplet, copy the dependency DLLs next to the executables before running them. Multi-config generators write binaries under `build/Debug` or `build/Release`. Single-config generators write them directly under `build/`.

## Run

| Binary | Purpose |
| --- | --- |
| `build/wft` | Loads the public item list and prints a few slugs containing `prime`. No account required. |
| `build/basic_usage` | Prompts for credentials, then searches items and converts a slug to an id. |
| `build/order_management` | Interactive menu that changes live orders on the signed-in account. |

`wft` needs a network connection. The examples need a Warframe.Market account. Password prompts use `std::getline` and the characters are visible while typing.

## Usage

`login` returns the token and does not store it. Call `setJWT` before `authenticated()`. Call `loadItems()` once before searching, converting ids, creating an order, or listing orders. Listing and creating orders look up slugs in that in-memory index.

```cpp
#include "wfm_api.h"

WarframeMarketAPI api;

AuthResponse auth = api.login(email, password);
api.setJWT(auth.token);
api.loadItems();

auto matches = api.searchItems("khora_prime");
std::string id = api.getIDBySlug(matches.front());

auto session = api.authenticated();

SellOrderRequest request;
request.itemSlug = matches.front();
request.platinum = 40;
request.quantity = 1;
request.visible = true;
std::string orderId = session.createSellOrder(request);

UpdateOrderRequest update;
update.hasPlatinum = true;
update.platinum = 35;
session.updateOrder(orderId, update);

session.closeOrder(orderId, 1);
```

`login` copies the `authorization` response header into `AuthResponse::token`. Pass that string to `setJWT`. Authenticated calls require the stored value to begin with `JWT `, including the space, and they send the remainder as a Bearer token.

`loadItems()` appends to the global slug list and does not clear it. Call it once per process. A second call duplicates search results.

The client spaces its own requests at least 350ms apart. `searchItems` is a case-sensitive substring match on the item slug.

## Project layout

```
include/wfm_api.h        high-level client
include/types.h          auth, order, and item structs
include/http_client.h    curl wrapper and rate spacing
include/constants.h      API base URLs and the item index type
src/constants.cpp        global itemDB definition
src/main.cpp             unauthenticated demo
examples/basic_usage.cpp login and item search
examples/order_management.cpp
                         live order menu
include/wfm_client.h     older interactive client
include/user.h           older holder around WfmClient
```

Link `src/constants.cpp` into any binary that calls `loadItems()`. The global `itemDB` is defined in that file.

## Install

```bash
cmake --install build --prefix /usr/local
```

Executables install to `bin`. Headers install to `include/wft`.

## License

MIT. See [LICENSE](LICENSE).
