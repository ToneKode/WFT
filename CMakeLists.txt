cmake_minimum_required(VERSION 3.15)

# ==================== Project Configuration ====================
project(wft VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==================== Dependencies ====================
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ==================== Include Paths ====================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ==================== Shared Sources ====================
set(SOURCES
    src/constants.cpp
)

# ==================== Targets ====================
# Main executable (simple demo)
add_executable(wft src/main.cpp ${SOURCES})
target_link_libraries(wft PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# Example executables
add_executable(basic_usage examples/basic_usage.cpp ${SOURCES})
target_link_libraries(basic_usage PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(order_management examples/order_management.cpp ${SOURCES})
target_link_libraries(order_management PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# ==================== Installation ====================
install(TARGETS wft basic_usage order_management
        RUNTIME DESTINATION bin)

install(DIRECTORY include/ 
        DESTINATION include/wft
        FILES_MATCHING PATTERN "*.h")
