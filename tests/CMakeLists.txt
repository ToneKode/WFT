# Tests link the same library the executables ship, so a green suite is
# evidence about the real code path rather than about a test-only copy.

find_package(Threads REQUIRED)

add_library(doctest_main STATIC doctest_main.cpp)
target_include_directories(doctest_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
wft_set_warnings(doctest_main)

add_executable(wft_tests
        test_order_book.cpp
        test_market_api.cpp
        test_item_cache.cpp
        test_wfm_socket.cpp
        test_ws_client.cpp
)
target_include_directories(wft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wft_tests PRIVATE wft_core doctest_main Threads::Threads)
wft_set_warnings(wft_tests)

add_test(NAME wft_tests COMMAND wft_tests)

# Give the suite a timeout so a hung network call in a future test fails
# instead of blocking CI. Failure is judged by exit code alone, so a test that
# legitimately prints "ERROR: " is not mistaken for a failure.
set_tests_properties(wft_tests PROPERTIES TIMEOUT 60)

# x64-mingw-dynamic and x64-windows link a shared libcurl. ctest starts
# wft_tests with a clean PATH, so the loader never sees libcurl.dll or the
# DLLs next to it (libssh2, zlib, libwebsockets, ...). Static triplets have
# no imported DLL and skip this.
if(WIN32)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _wft_cfg)
    set(_wft_curl_dll "")
    if(_wft_cfg)
        get_target_property(_wft_curl_dll CURL::libcurl "IMPORTED_LOCATION_${_wft_cfg}")
    endif()
    if(NOT _wft_curl_dll OR _wft_curl_dll MATCHES "NOTFOUND")
        get_target_property(_wft_curl_dll CURL::libcurl IMPORTED_LOCATION)
    endif()
    get_target_property(_wft_curl_type CURL::libcurl TYPE)
    if(_wft_curl_type STREQUAL "SHARED_LIBRARY"
            AND _wft_curl_dll
            AND NOT _wft_curl_dll MATCHES "NOTFOUND")
        get_filename_component(_wft_curl_bindir "${_wft_curl_dll}" DIRECTORY)
        file(TO_CMAKE_PATH "${_wft_curl_bindir}" _wft_curl_bindir)
        set_tests_properties(wft_tests PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_wft_curl_bindir}")
    endif()
    unset(_wft_cfg)
    unset(_wft_curl_dll)
    unset(_wft_curl_type)
    unset(_wft_curl_bindir)
endif()
