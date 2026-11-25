cmake_minimum_required(VERSION 3.23)

# ===========================================================================
# vcpkg bootstrap
#
# Toolchain files must be set before project(), and vcpkg reads the triplet
# during project(), so both live here. This is what lets a fresh clone build
# with `cmake --preset default` and no manual flags.
#
# Disable with -DWFT_BOOTSTRAP_VCPKG=OFF when the machine already provides curl
# and nlohmann_json itself.
# ===========================================================================
option(WFT_BOOTSTRAP_VCPKG "Auto-detect a vcpkg toolchain when none is provided" ON)
if(WFT_BOOTSTRAP_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_wft_vcpkg_candidates "")
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _wft_vcpkg_candidates "$ENV{VCPKG_ROOT}")
    endif()
    list(APPEND _wft_vcpkg_candidates
            "$ENV{USERPROFILE}/vcpkg"
            "$ENV{HOME}/vcpkg"
            "/opt/vcpkg"
            "/usr/local/share/vcpkg")

    foreach(_candidate IN LISTS _wft_vcpkg_candidates)
        if(EXISTS "${_candidate}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_candidate}/scripts/buildsystems/vcpkg.cmake"
                    CACHE FILEPATH "vcpkg toolchain (auto-detected)")
            message(STATUS "wft: using vcpkg toolchain at ${_candidate}")
            break()
        endif()
    endforeach()

    unset(_wft_vcpkg_candidates)
    unset(_candidate)
endif()

# One dependency contract for every OS: the triplet follows the compiler in
# use, so no preset names a platform. MinGW cannot share MSVC's x64-windows
# runtime (the mismatch only shows up at link time), so those differ.
# An explicit -DVCPKG_TARGET_TRIPLET or VCPKG_DEFAULT_TRIPLET still wins.
if(DEFINED CMAKE_TOOLCHAIN_FILE
        AND NOT DEFINED VCPKG_TARGET_TRIPLET
        AND NOT DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(VCPKG_TARGET_TRIPLET "arm64-osx" CACHE STRING "vcpkg triplet")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-osx" CACHE STRING "vcpkg triplet")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg triplet")
    else()
        # Windows. CMAKE_CXX_COMPILER is not set before project(), so probe the
        # same compilers CMake will consider, in the same order.
        set(_wft_cxx "${CMAKE_CXX_COMPILER}")
        if(NOT _wft_cxx AND DEFINED ENV{CXX})
            set(_wft_cxx "$ENV{CXX}")
        endif()
        if(NOT _wft_cxx)
            find_program(_wft_cxx_cl NAMES cl)
            if(_wft_cxx_cl)
                set(_wft_cxx "${_wft_cxx_cl}")
            endif()
        endif()
        if(NOT _wft_cxx)
            find_program(_wft_cxx_gxx NAMES g++)
            if(_wft_cxx_gxx)
                set(_wft_cxx "${_wft_cxx_gxx}")
            endif()
        endif()
        if(_wft_cxx MATCHES "cl(\\.exe)?$")
            set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet")
        elseif(_wft_cxx)
            execute_process(COMMAND "${_wft_cxx}" -dumpmachine
                    OUTPUT_VARIABLE _wft_machine
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
            if(_wft_machine MATCHES "mingw")
                set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg triplet")
            else()
                set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet")
            endif()
            unset(_wft_machine)
        endif()
        unset(_wft_cxx)
        unset(_wft_cxx_cl)
        unset(_wft_cxx_gxx)
    endif()
    if(VCPKG_TARGET_TRIPLET)
        message(STATUS "wft: triplet for this compiler: ${VCPKG_TARGET_TRIPLET}")
    endif()
endif()

project(wft VERSION 1.0.0 LANGUAGES CXX)

# ===========================================================================
# Options
# ===========================================================================
option(WFT_WARNINGS_AS_ERRORS "Promote compiler warnings to errors" OFF)

include(CTest) # provides BUILD_TESTING (ON by default) and enables `ctest`
include(cmake/WftDependencies.cmake)
include(cmake/WftWarnings.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ===========================================================================
# Core library
#
# Header-only. Tests and the demo compile the same headers.
# ===========================================================================
add_library(wft_core INTERFACE)
target_include_directories(wft_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(wft_core INTERFACE CURL::libcurl nlohmann_json::nlohmann_json)

# ===========================================================================
# Executables
# ===========================================================================
add_executable(wft src/main.cpp)
target_link_libraries(wft PRIVATE wft_core)
wft_set_warnings(wft)

# A dynamic vcpkg triplet leaves the curl DLL in the vcpkg tree, so the demo
# only starts from a shell with that bin directory on PATH. Put it alongside
# the executable instead. A shared system library (Linux, macOS) is already
# found at runtime, so this stays on Windows.
if(WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "windows|mingw-dynamic")
    get_target_property(_wft_curl_type CURL::libcurl TYPE)
    if(_wft_curl_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET wft POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:CURL::libcurl>" "$<TARGET_FILE_DIR:wft>"
                VERBATIM)
    endif()
    unset(_wft_curl_type)
endif()

# ===========================================================================
# Tests
# ===========================================================================
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ===========================================================================
# Installation
# ===========================================================================
install(TARGETS wft
        RUNTIME DESTINATION bin)

install(DIRECTORY include/
        DESTINATION include/wft
        FILES_MATCHING PATTERN "*.h")
