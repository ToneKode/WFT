/**
 * @file main.cpp
 * @brief Read a public item count and one item's best sell.
 *
 * Hits the live API. The test suite does not.
 */

#include <iostream>
#include "market_api.h"

int main()
{
    try
    {
        MarketAPI api;

        const std::vector<Item> items = api.items();
        std::cout << items.size() << " items\n";
        if (items.empty())
        {
            return 0;
        }

        const std::string slug = items.front().slug;
        const OrderBook book = api.bookForItem(slug);
        double perUnit = 0.0;
        if (book.bestSell(perUnit))
        {
            std::cout << slug << " best sell: " << perUnit << " plat per unit\n";
        }
        else
        {
            std::cout << slug << " has no sell orders\n";
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
