# AGENTS.md

Working notes for coding agents (and humans) in this repo. Follow these
commands verbatim; they are the tested path.

## What this is

A small C++14 client library for the Warframe.Market API. Header-heavy: most
logic lives in `include/*.h`, with one `.cpp` for the global item database.

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
| `include/wfm_api.h` | **Primary API.** `WarframeMarketAPI` — auth, items, orders |
| `include/wfm_client.h` | Legacy interactive client (`WfmClient`), used by `include/user.h` |
| `include/http_client.h` | libcurl implementation of `HttpTransport`; rate limits to 350 ms |
| `include/http_transport.h` | Transport interface; the seam that makes tests possible |
| `include/constants.h` | `V1_API` / `V2_API` base URLs, global `itemDB` |
| `include/types.h` | Request/response models |
| `src/constants.cpp` | Defines the global `itemDB` |
| `tests/` | doctest suite; `tests/fake_transport.h` is the offline transport |
| `examples/` | Runnable demos; built as separate executables |

`wft_core` is the library target. Executables and tests link it, so tests
exercise the same code that ships.

## Things that will bite you

- **`itemDB` is a process-wide global.** Tests that touch it call
  `resetGlobalItemDb()` first (see `tests/test_api.cpp`). Order-dependent tests
  will pass locally and fail in isolation — always reset.
- **Never call the live API in a test.** Construct
  `WarframeMarketAPI(std::unique_ptr<HttpTransport>(new FakeTransport()))` and
  queue replies. `FakeTransport` throws immediately if a test forgets to queue
  a reply, so a leaked network call fails fast instead of hanging.
- **`FakeTransport` replies are FIFO**, one per request. A test that makes N
  requests queues N replies.
- **Auth is a state machine** (`UnauthenticatedState`/`AuthenticatedState`).
  Order methods go through `api.authenticated()`, which throws unless
  `setJWT()` set a non-empty token.
- **`setJWT("")` clears auth.** Use `clearJWT()` for intent.
- **Tokens are `"JWT <token>"`.** `extractBearerToken()` throws on any other
  shape; the header sends `Bearer <token>`.
- **`http_client.h` and `wfm_client.h` define `using json = nlohmann::json;`
  at global scope.** Including both in one translation unit is fine (identical
  alias), but do not introduce a different `json` alias.
- **`include/user.h` has a `WfmClient&` reference member and no default ctor.**
  It is largely unfinished; do not build on it without cleaning it up first.
- **`loadItems()` is required before slug↔id lookups.** Lookups on an empty
  database throw.

## Conventions

- C++14. No `std::optional` in the library headers (it appears in the legacy
  `wfm_client.h`); the request structs use `hasX` flags instead.
- 4-space indent, braces on the same line, 100-column limit (`.clang-format`).
- Match the existing doc-comment style (`@brief`, `@param`, `@return`) on
  public API.
- Prefer fixing a warning over suppressing it.

## Definition of done

1. `cmake --build --preset mingw` (or `linux`) completes with no warnings.
2. `ctest --preset mingw` passes.
3. New behaviour has a test in `tests/` that runs offline via `FakeTransport`.
4. Public API changes are reflected in `README.md`.
