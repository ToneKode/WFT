# Tests link the same library the executables ship, so a green suite is
# evidence about the real code path rather than about a test-only copy.

find_package(Threads REQUIRED)

add_library(doctest_main STATIC doctest_main.cpp)
target_include_directories(doctest_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
wft_set_warnings(doctest_main)

add_executable(wft_tests
        test_item_database.cpp
        test_api.cpp
        test_order_book.cpp
        test_market_api.cpp
        test_wfm_socket.cpp
)
target_include_directories(wft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wft_tests PRIVATE wft_core doctest_main Threads::Threads)
wft_set_warnings(wft_tests)

add_test(NAME wft_tests COMMAND wft_tests)

# Give the suite a timeout so a hung network call in a future test fails
# instead of blocking CI. Failure is judged by exit code alone, so a test that
# legitimately prints "ERROR: " is not mistaken for a failure.
set_tests_properties(wft_tests PROPERTIES TIMEOUT 60)
