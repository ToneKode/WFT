cmake_minimum_required(VERSION 3.23)

# ===========================================================================
# vcpkg bootstrap
#
# Toolchain files must be set before project(), and vcpkg reads the triplet
# during project(), so both live here. This is what lets a fresh clone build
# with `cmake --preset mingw` and no manual flags.
#
# Disable with -DWFT_BOOTSTRAP_VCPKG=OFF when the platform provides curl and
# nlohmann_json itself (e.g. the Linux CI image).
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

# MinGW needs vcpkg's matching community triplet; the default (x64-windows) is
# built against the MSVC runtime and fails at link time with an error that
# never mentions triplets. Default it here so bare `cmake -S . -B build` works.
if(DEFINED CMAKE_TOOLCHAIN_FILE
        AND NOT DEFINED VCPKG_TARGET_TRIPLET
        AND NOT DEFINED ENV{VCPKG_DEFAULT_TRIPLET}
        AND NOT CMAKE_GENERATOR MATCHES "Visual Studio|Ninja")
    set(_wft_probe_compiler "${CMAKE_CXX_COMPILER}")
    if(NOT _wft_probe_compiler)
        set(_wft_probe_compiler "g++")
    endif()
    execute_process(
            COMMAND "${_wft_probe_compiler}" -dumpmachine
            OUTPUT_VARIABLE _wft_target_machine
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    if(_wft_target_machine MATCHES "mingw")
        set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg triplet")
        message(STATUS "wft: MinGW compiler detected, using triplet ${VCPKG_TARGET_TRIPLET}")
    endif()
    unset(_wft_probe_compiler)
    unset(_wft_target_machine)
endif()

project(wft VERSION 1.0.0 LANGUAGES CXX)

# ===========================================================================
# Options
# ===========================================================================
option(WFT_BUILD_EXAMPLES "Build the example programs" ON)
option(WFT_WARNINGS_AS_ERRORS "Promote compiler warnings to errors" OFF)

include(CTest) # provides BUILD_TESTING (ON by default) and enables `ctest`
include(cmake/WftDependencies.cmake)
include(cmake/WftWarnings.cmake)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ===========================================================================
# Core library
#
# Everything the repo is worth testing lives behind this target so that tests
# link the same code the executables ship, without going through main().
# ===========================================================================
add_library(wft_core STATIC
        src/constants.cpp
)

target_include_directories(wft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(wft_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
wft_set_warnings(wft_core)

# ===========================================================================
# Executables
# ===========================================================================
add_executable(wft src/main.cpp)
target_link_libraries(wft PRIVATE wft_core)
wft_set_warnings(wft)

if(WFT_BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE wft_core)
    wft_set_warnings(basic_usage)

    add_executable(order_management examples/order_management.cpp)
    target_link_libraries(order_management PRIVATE wft_core)
    wft_set_warnings(order_management)
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
set(WFT_INSTALL_TARGETS wft wft_core)
if(WFT_BUILD_EXAMPLES)
    list(APPEND WFT_INSTALL_TARGETS basic_usage order_management)
endif()

install(TARGETS ${WFT_INSTALL_TARGETS}
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib)

install(DIRECTORY include/
        DESTINATION include/wft
        FILES_MATCHING PATTERN "*.h")
