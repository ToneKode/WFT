/**
 * @file main.cpp
 * @brief Simple demonstration of the Warframe Market API client
 */

#include <iostream>
#include "../include/wfm_api.h"

int main()
{
    try
    {
        WarframeMarketAPI api;
        
        std::cout << "=== Warframe Market API Client ===" << std::endl;
        std::cout << "\nThis is a C++ API client for Warframe.Market" << std::endl;
        std::cout << "Check the examples/ folder for usage demonstrations:" << std::endl;
        std::cout << "  - basic_usage.cpp      : Authentication and item search" << std::endl;
        std::cout << "  - order_management.cpp : Full order management workflow" << std::endl;
        
        std::cout << "\n--- Quick Demo ---" << std::endl;
        
        // Load items (no authentication needed)
        std::cout << "Loading items..." << std::endl;
        size_t itemCount = api.loadItems();
        std::cout << "✓ Loaded " << itemCount << " tradeable items" << std::endl;
        
        // Search example
        std::cout << "\nSearching for 'prime' items..." << std::endl;
        auto results = api.searchItems("prime");
        std::cout << "✓ Found " << results.size() << " items containing 'prime'" << std::endl;
        
        std::cout << "\nFirst 5 results:" << std::endl;
        for (size_t i = 0; i < std::min(results.size(), size_t(5)); ++i)
        {
            std::cout << "  " << (i + 1) << ". " << results[i] << std::endl;
        }
        
        std::cout << "\n=== Demo complete ===" << std::endl;
        std::cout << "For full functionality, see the example programs." << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
