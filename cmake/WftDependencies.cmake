# ===========================================================================
# Third-party dependency resolution
#
# vcpkg toolchain and triplet selection happen at the top of CMakeLists.txt,
# before project(), because that is when they take effect. The manifest
# (vcpkg.json) is the one dependency contract: curl with WebSockets plus
# nlohmann_json, on every OS.
# ===========================================================================

# Give find_package a chance to see a vcpkg-style installed tree even when no
# toolchain file was used (WFT_BOOTSTRAP_VCPKG=OFF on a machine that already
# ran `vcpkg install`).
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    foreach(_triplet "" x64-mingw-dynamic x64-windows x64-linux arm64-osx x64-osx)
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}/installed/${_triplet}" _maybe_prefix)
        if(EXISTS "${_maybe_prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${_maybe_prefix}")
        endif()
    endforeach()
    unset(_maybe_prefix)
    unset(_triplet)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.10 CONFIG REQUIRED)

# WfmSocket speaks the protocol, but the WebSocket transport it is driven by
# needs a curl that was built with the feature. Catch that at configure time.
# shortcut: detection uses the found curl's metadata, which vcpkg's config and
# pkg-config both publish; a bare module-mode find publishes nothing, upgrade
# to a try_compile when that path matters.
if(DEFINED CURL_SUPPORTED_PROTOCOLS AND NOT "WS" IN_LIST CURL_SUPPORTED_PROTOCOLS)
    message(FATAL_ERROR
            "The curl found was built without WebSockets.\n"
            "Install curl[websockets] (the vcpkg manifest in vcpkg.json already does).")
endif()
