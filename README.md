# Warframe Market API Client (C++)

A clean, modern C++ API client library for [Warframe.Market](https://warframe.market). This library provides a simple interface for day to day sellers of warframe market.

The library will continue to expand with more functionalities. For now, the implemented/will implement functions are listed below:

    auth
        ✅ sign in
        🔲 sign out
    profile
        🔲 get current user's profile
        🔲 manage current user profile
        🔲 get a user's profile
    items
        ✅ list all tradable items
        ✅ search for a item's slug
    statistics
        🔲 get statistics of an item
        🔲 get global market statistics
    orders
        🔲 get orders of a single item
        🔲 get orders for the last 4 hours
        ✅ update a single order on the current profile
        ✅ delete a single order on the current profile
        ✅ close a single order on the current profile
        ✅ add a new order for the current profile
        ✅ get all of your own orders

Symbols:

    ✅: implemented
    🔲: not implemented yet


## Project Structure

```
wft/
├── include/
│   ├── wfm_api.h        # Main API client (clean interface)
│   ├── wfm_client.h     # Legacy client (deprecated)
│   ├── http_client.h    # HTTP operations
│   ├── types.h          # Data structures
│   ├── constants.h      # API constants and item database
│   └── user.h           # User class (deprecated)
├── src/
│   ├── main.cpp         # Simple demo application
│   └── constants.cpp    # ItemDatabase implementation
├── examples/
│   ├── basic_usage.cpp      # Authentication and search
│   └── order_management.cpp # Full order workflow
├── build/               # Build output directory
└── CMakeLists.txt       # Build configuration
```

## Dependencies

- **C++17** or higher
- **CMake** 3.15+
- **libcurl** - HTTP requests
- **nlohmann/json** - JSON parsing
- **vcpkg** (recommended for dependency management)

## Building the Project

### Windows (vcpkg)

1. **Install vcpkg** (if not already installed):
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Install dependencies**:
   ```bash
   vcpkg install curl nlohmann-json
   ```

3. **Clone and build**:
   ```bash
   git clone <your-repo-url>
   cd cppWFT
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
   cmake --build .
   ```

4. **Copy DLLs** (if using dynamic linking):
   ```bash
   copy <vcpkg-path>\installed\x64-mingw-dynamic\bin\*.dll build\
   ```

### Linux/macOS

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev nlohmann-json3-dev cmake g++

# Build
mkdir build && cd build
cmake ..
make
```

## Usage

### Quick Start

```cpp
#include "include/wfm_api.h"

int main()
{
    WarframeMarketAPI api;
    
    // Authenticate
    auto auth = api.login("your-email@example.com", "your-password");
    api.setJWT(auth.token);
    
    // Load items database
    api.loadItems();
    
    // Search for items
    auto results = api.searchItems("khora");
    
    // Create a sell order
    std::string orderId = api.createSellOrder("khora_prime_set", 150, 1);
    
    return 0;
}
```

### Running Examples

The project includes two complete example applications:

**1. Basic Usage** - Authentication and item search:
```bash
cd build
./basic_usage
```

**2. Order Management** - Interactive order management:
```bash
cd build
./order_management
```

##  API Reference

### WarframeMarketAPI Class

#### Authentication
```cpp
AuthResponse login(const std::string& email, const std::string& password, 
                   const std::string& device_id = "");
void setJWT(const std::string& token);
std::string getJWT() const;
```

#### Item Database
```cpp
size_t loadItems();  // Load all items from API
std::vector<std::string> searchItems(const std::string& query);
std::string getSlugByID(const std::string& id);
std::string getIDBySlug(const std::string& slug);
size_t getItemCount() const;
```

#### Order Management
```cpp
std::vector<OrderDetail> getMyOrders();
std::string createSellOrder(const std::string& itemSlug, int platinum, 
                            int quantity, bool visible = true, 
                            std::optional<int> rank = std::nullopt);
void updateOrder(const std::string& orderId, 
                std::optional<int> platinum = std::nullopt,
                std::optional<int> quantity = std::nullopt,
                std::optional<bool> visible = std::nullopt,
                std::optional<int> rank = std::nullopt);
void closeOrder(const std::string& orderId, int quantity);
void deleteOrder(const std::string& orderId);
```

## Architecture

The library is designed with clean separation of concerns:

- **`wfm_api.h`** - High-level API client (use this!)
- **`http_client.h`** - Low-level HTTP operations with rate limiting
- **`types.h`** - Data structures (AuthResponse, OrderDetail, etc.)
- **`constants.h`** - API endpoints and ItemDatabase

All API methods throw `std::runtime_error` on failure.

## Building from Source

### Windows (vcpkg)

1. **Install vcpkg** (if not already installed):
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Install dependencies**:
   ```bash
   vcpkg install curl nlohmann-json
   ```

3. **Build the project**:
   ```bash
   cd wft
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
   cmake --build .
   ```

4. **Copy DLLs** (if using dynamic linking):
   ```bash
   copy <vcpkg-path>\installed\x64-mingw-dynamic\bin\*.dll .
   ```

### Linux/macOS

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev nlohmann-json3-dev cmake g++

# Build
mkdir build && cd build
cmake ..
make
```

## API Endpoints

This client uses the following Warframe.Market API v1/v2 endpoints:

- `POST /v1/auth/signin` - Authentication
- `GET /v2/items` - Fetch all tradeable items
- `GET /v2/orders/my` - Get user's orders
- `POST /v2/order/` - Create new order
- `PATCH /v2/order/{id}` - Update existing order
- `POST /v2/order/{id}/close` - Close order (mark as sold)
- `DELETE /v2/order/{id}` - Delete order

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Disclaimer

This is an **unofficial** client. Not affiliated with Digital Extremes or Warframe.Market.

## Acknowledgments

- [Warframe.Market](https://warframe.market) for providing the trading platform and API
- [nlohmann/json](https://github.com/nlohmann/json) for the excellent JSON library
- [libcurl](https://curl.se/libcurl/) for HTTP functionality
- The Warframe trading community

---