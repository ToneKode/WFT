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
# From the repo root, with the project already configured:
cmake --build --preset mingw --target basic_usage
./build/mingw/basic_usage
```

### 2. order_management.cpp
Full-featured order management application with interactive menu:
- View all your current orders
- Create new orders (buy or sell), prompting only for the fields the item needs
- Update existing orders (price, quantity, visibility)
- Close orders (partially or fully)
- Delete orders

**Usage:**
```bash
# From the repo root, with the project already configured:
cmake --build --preset mingw --target order_management
./build/mingw/order_management
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
- Rate limiting (350ms between requests) is automatic, with retry on 429/509
- Order creation is validated against the item's own metadata before sending,
  so the examples prompt only for fields the chosen item actually needs
- Examples are designed to be educational and demonstrate best practices
