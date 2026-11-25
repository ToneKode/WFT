/**
 * @file main.cpp
 * @brief Read a public item count and one item's best sell.
 *
 * Hits the live API. The test suite does not.
 */

#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
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
        const std::optional<double> bestSell = api.bookForItem(slug).bestSell();
        if (bestSell)
        {
            std::cout << std::format("{} best sell: {} plat per unit\n", slug, *bestSell);
        }
        else
        {
            std::cout << std::format("{} has no sell orders\n", slug);
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
