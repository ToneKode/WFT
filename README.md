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

- CMake 3.15+
- C++14 compiler
- libcurl
- nlohmann_json

## Build

### Windows (vcpkg)

1. Install dependencies:

	 vcpkg install curl nlohmann-json

2. Configure and build:

	 cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
	 cmake --build build

3. If using dynamic triplets, copy DLLs into build/ before running.

### Linux/macOS

1. Install dependencies with your package manager.
2. Configure and build:

	 cmake -S . -B build
	 cmake --build build

## Run

- Main demo: build/wft
- Basic example: build/basic_usage
- Order management example: build/order_management

## Project Layout

- include/wfm_api.h: high-level API
- include/types.h: request and response models
- include/http_client.h: HTTP methods and rate limiting
- include/constants.h: API constants and in-memory item index
- src/main.cpp: simple demo
- examples/basic_usage.cpp: auth and item search flow
- examples/order_management.cpp: interactive order workflow

## License

MIT. See LICENSE.
