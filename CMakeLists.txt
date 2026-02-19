cmake_minimum_required(VERSION 3.15)
project(wft VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/constants.cpp
)

# Main executable (simple demo)
add_executable(wft src/main.cpp ${SOURCES})
target_link_libraries(wft PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# Example executables
add_executable(basic_usage examples/basic_usage.cpp ${SOURCES})
target_link_libraries(basic_usage PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(order_management examples/order_management.cpp ${SOURCES})
target_link_libraries(order_management PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# Installation
install(TARGETS wft basic_usage order_management
        RUNTIME DESTINATION bin)

install(DIRECTORY include/ 
        DESTINATION include/wft
        FILES_MATCHING PATTERN "*.h")
