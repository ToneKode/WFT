# wft

Small C++ client library for the Warframe.Market API.

## What This Repo Includes

- Core API client in include/wfm_api.h
- HTTP layer with libcurl in include/http_client.h
- Item lookup database in include/constants.h and src/constants.cpp
- Two runnable examples in examples/

## Implemented Features

- Authentication
	- Sign in with email and password
	- Set or clear JWT on the client
- Item data
	- Load all tradable items
	- Search item slugs by substring
	- Convert item ID <-> slug
- Authenticated order operations
	- List your orders
	- Create sell order
	- Update order fields
	- Close order
	- Delete order

Note: Authenticated order methods are exposed through WarframeMarketAPI::authenticated().

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

     vcpkg install curl nlohmann-json --triplet x64-mingw-dynamic

3. Keep vcpkg at `C:\Users\<you>\vcpkg` (or set `VCPKG_ROOT`). The build
   finds it automatically; no toolchain flags needed.

If you prefer a bare `cmake -S . -B build`, pass
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` and
`-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic`.

### Linux/macOS setup

1. Install `libcurl` and `nlohmann_json` with your package manager.
2. Configure and build:

     cmake --preset linux
     cmake --build --preset linux
     ctest --preset linux

## Run

- Main demo: build/mingw/wft
- Basic example: build/mingw/basic_usage
- Order management example: build/mingw/order_management

Note: dynamic vcpkg triplets require the curl/nlohmann DLLs beside the
executable. If a binary fails to start, copy them from
`vcpkg/installed/x64-mingw-dynamic/bin` into the build directory.

## Testing

```bash
ctest --preset mingw
```

Tests run fully offline: `tests/fake_transport.h` implements `HttpTransport`
with queued canned responses, so nothing in the suite touches the real API or
needs credentials.

## Project Layout

- include/wfm_api.h: high-level API
- include/types.h: request and response models
- include/http_client.h: HTTP methods and rate limiting
- include/http_transport.h: transport interface (test seam)
- include/constants.h: API constants and in-memory item index
- src/main.cpp: simple demo
- tests/: doctest suite (offline via fake_transport.h)
- examples/basic_usage.cpp: auth and item search flow
- examples/order_management.cpp: interactive order workflow

## Contributing

See `AGENTS.md` for the tested build commands, project conventions, and the
non-obvious gotchas (global item database, auth state machine, MinGW triplet).

## License

MIT. See LICENSE.
