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
| `include/order_validation.h` | Per-item order rules; `ValidationError` names the bad field |
| `include/wfm_json.h` | v2 JSON → model mapping (`wfm_json::parseOrder`, etc.) |
| `include/api_error.h` | v2 error envelope parsing; `ApiError` carries `request`/`inputs` codes |
| `include/wfm_client.h` | Legacy interactive console demo over `WarframeMarketAPI` |
| `include/http_client.h` | libcurl implementation of `HttpTransport`; 350 ms pacing, 429/509 retry |
| `include/http_transport.h` | Transport interface; the seam that makes tests possible |
| `include/constants.h` | `V1_API` / `V2_API` / `WS_API`, `WFT_USER_AGENT`, global `itemDB` |
| `include/types.h` | Request/response models |
| `src/constants.cpp` | Defines the global `itemDB` |
| `tests/` | doctest suite; `tests/fake_transport.h` is the offline transport |
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
- **`include/user.h` is still unfinished** and has a `WfmClient&` reference
 member with no default ctor. `WfmClient` now wraps `WarframeMarketAPI`, so
 prefer the API directly rather than building on `user.h`.
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

## Definition of done

1. `cmake --build --preset mingw` (or `linux`) completes with no warnings.
2. `ctest --preset mingw` passes.
3. New behaviour has a test in `tests/` that runs offline via `FakeTransport`.
4. Public API changes are reflected in `README.md`.
