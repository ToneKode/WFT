# Examples

This directory contains usage examples for the Warframe Market API client.

## Available Examples

### 1. basic_usage.cpp
Demonstrates fundamental API operations:
- User authentication
- Loading the items database
- Searching for items by name
- Converting between item slugs and IDs

**Usage:**
```bash
# Compile
g++ -std=c++17 basic_usage.cpp ../src/constants.cpp -I../include -lcurl -o basic_usage

# Run
./basic_usage
```

### 2. order_management.cpp
Full-featured order management application with interactive menu:
- View all your current orders
- Create new sell orders
- Update existing orders (price, quantity, visibility)
- Close orders (mark as sold)
- Delete orders

**Usage:**
```bash
# Compile
g++ -std=c++17 order_management.cpp ../src/constants.cpp -I../include -lcurl -o order_management

# Run
./order_management
```

## Building with CMake

You can also build the examples using CMake. See the main project README for build instructions.

## Requirements

- Valid Warframe.Market account credentials
- Active internet connection
- All dependencies listed in main README

## Notes

- The examples use `std::getline()` for secure password input
- All API responses include error handling
- Rate limiting (350ms between requests) is automatic
- Examples are designed to be educational and demonstrate best practices
