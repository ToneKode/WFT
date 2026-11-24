# ===========================================================================
# Third-party dependency resolution
#
# vcpkg toolchain and triplet selection happen at the top of CMakeLists.txt,
# before project(), because that is when they take effect.
# ===========================================================================

# Give find_package a chance to see a vcpkg-style installed tree even when no
# toolchain file was used (a box that already ran `vcpkg install`).
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    foreach(_triplet "" x64-mingw-dynamic x64-windows x64-linux)
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}/installed/${_triplet}" _maybe_prefix)
        if(EXISTS "${_maybe_prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${_maybe_prefix}")
        endif()
    endforeach()
    unset(_maybe_prefix)
    unset(_triplet)
endif()

if(MINGW AND DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    message(FATAL_ERROR
            "MinGW detected but VCPKG_TARGET_TRIPLET is not set.\n"
            "Use:  cmake --preset mingw\n"
            "or pass -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic")
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.10 CONFIG REQUIRED)
