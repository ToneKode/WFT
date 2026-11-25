# Examples

Interactive programs for `WarframeMarketAPI`. Both talk to the live Warframe.Market API.

Build them from the repository root with the project CMake target. That links `src/constants.cpp`, libcurl, and nlohmann/json, and compiles as C++14:

```bash
cmake -S . -B build
cmake --build build
```

The binaries are `build/basic_usage` and `build/order_management` for a single-config generator. A multi-config generator places them under `build/Debug` or `build/Release`.

You can also compile one file when `nlohmann/json.hpp` and libcurl are already on the compiler's default search paths:

```bash
g++ -std=c++14 basic_usage.cpp ../src/constants.cpp -I../include -lcurl -o basic_usage
g++ -std=c++14 order_management.cpp ../src/constants.cpp -I../include -lcurl -o order_management
```

`src/constants.cpp` has to be in the link. It defines the global item index that `loadItems()` fills.

## basic_usage.cpp

Walks through the read path:

1. Prompts for email and password and calls `login`, then `setJWT`.
2. Loads the tradable item list.
3. Searches slugs for the substring you type.
4. Converts the first hit from slug to id and back.

Run it with `./build/basic_usage` from the repository root, or `./basic_usage` if you used the `g++` command above. It needs a network connection and a Warframe.Market account. The password is read with `std::getline` and is visible while you type. On success the program prints the in-game name, the user id, and the first 20 characters of the JWT.

## order_management.cpp

Signs in, loads the item index, then offers a menu:

1. View current orders.
2. Create a sell order. Search results are capped at 10 on screen, and the selection is taken from that list.
3. Update platinum, quantity, or visibility. Press Enter on a field to leave it unchanged. A rank update is available on `UpdateOrderRequest::hasRank` and is left to calling code.
4. Close an order by id and quantity sold.
5. Delete an order after you type `yes`.

Run it with `./build/order_management`. Menu choices 2 through 5 change orders on the signed-in account.

Order listing and sell-order creation resolve item slugs through the index from `loadItems()`. The example loads that index before it opens the menu.

## Runtime notes

- Login and item-load failures are caught in `main`, printed, and exit with status 1. Create, update, close, and delete catch errors from the mutating call and return to the menu. Those actions list orders before that catch, so a listing failure still exits.
- Requests from one client are spaced at least 350ms apart.
- Credentials stay in the process. They are not written to a file.
