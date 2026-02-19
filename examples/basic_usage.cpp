/**
 * @file basic_usage.cpp
 * @brief Basic usage example for Warframe Market API
 * 
 * This example demonstrates:
 * - Authentication
 * - Loading items
 * - Searching for items
 * - Converting between slugs and IDs
 */

#include <iostream>
#include <string>
#include "../include/wfm_api.h"

int main()
{
    try
    {
        WarframeMarketAPI api;
        
        // ==================== Authentication ====================
        std::cout << "=== Warframe Market API - Basic Usage ===" << std::endl;
        std::cout << "\n--- Authentication ---" << std::endl;
        
        std::string email, password;
        std::cout << "Enter email: ";
        std::getline(std::cin, email);
        std::cout << "Enter password: ";
        std::getline(std::cin, password);
        
        auto auth = api.login(email, password);
        api.setJWT(auth.token);
        
        std::cout << "✓ Login successful!" << std::endl;
        std::cout << "  User: " << auth.ingame_name << std::endl;
        std::cout << "  User ID: " << auth.user_id << std::endl;
        std::cout << "  Token: " << auth.token.substr(0, 20) << "..." << std::endl;
        
        // ==================== Load Items ====================
        std::cout << "\n--- Loading Items ---" << std::endl;
        size_t itemCount = api.loadItems();
        std::cout << "✓ Loaded " << itemCount << " items" << std::endl;
        
        // ==================== Search Items ====================
        std::cout << "\n--- Search Items ---" << std::endl;
        std::string query;
        std::cout << "Enter item name to search (e.g., 'khora'): ";
        std::getline(std::cin, query);
        
        auto results = api.searchItems(query);
        if (!results.empty())
        {
            std::cout << "✓ Found " << results.size() << " item(s):" << std::endl;
            for (size_t i = 0; i < std::min(results.size(), size_t(10)); ++i)
            {
                std::cout << "  " << (i + 1) << ". " << results[i] << std::endl;
            }
            if (results.size() > 10)
            {
                std::cout << "  ... and " << (results.size() - 10) << " more" << std::endl;
            }
        }
        else
        {
            std::cout << "No items found matching: " << query << std::endl;
        }
        
        // ==================== Slug/ID Conversion ====================
        if (!results.empty())
        {
            std::cout << "\n--- Slug/ID Conversion ---" << std::endl;
            std::string slug = results[0];
            std::string id = api.getIDBySlug(slug);
            std::cout << "Slug: " << slug << std::endl;
            std::cout << "ID:   " << id << std::endl;
            
            // Verify round-trip conversion
            std::string slug_back = api.getSlugByID(id);
            std::cout << "✓ Round-trip conversion successful: " << (slug == slug_back ? "PASS" : "FAIL") << std::endl;
        }
        
        std::cout << "\n=== Basic usage complete ===" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
