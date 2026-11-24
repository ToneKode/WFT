/**
 * @file order_management.cpp
 * @brief Order management example for Warframe Market API
 * 
 * This example demonstrates:
 * - Viewing current orders
 * - Creating sell orders
 * - Updating orders
 * - Closing orders
 * - Deleting orders
 */

#include <iostream>
#include <string>
#include <iomanip>
#include <limits>
#include "../include/wfm_api.h"

void displayOrders(const std::vector<OrderDetail>& orders)
{
    if (orders.empty())
    {
        std::cout << "No orders found." << std::endl;
        return;
    }
    
    std::cout << "\n┌─────────────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│                    Current Orders                           │" << std::endl;
    std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
    
    for (size_t i = 0; i < orders.size(); ++i)
    {
        const auto& order = orders[i];
        std::cout << "│ Order #" << (i + 1) << std::endl;
        std::cout << "│   ID:       " << order.id << std::endl;
        std::cout << "│   Type:     " << order.orderType << std::endl;
        std::cout << "│   Item:     " << order.itemSlug << std::endl;
        std::cout << "│   Price:    " << order.platinum << " platinum" << std::endl;
        std::cout << "│   Quantity: " << order.quantity << std::endl;
        std::cout << "│   Visible:  " << (order.visible ? "Yes" : "No") << std::endl;
        if (i < orders.size() - 1)
        {
            std::cout << "├─────────────────────────────────────────────────────────────┤" << std::endl;
        }
    }
    std::cout << "└─────────────────────────────────────────────────────────────┘" << std::endl;
}

void createOrder(WarframeMarketAPI& api, WarframeMarketAPI::AuthenticatedClient& session)
{
    std::cout << "\n--- Create Sell Order ---" << std::endl;
    
    // Search for item
    std::string query;
    std::cout << "Enter item name to search: ";
    std::getline(std::cin, query);
    
    auto results = api.searchItems(query);
    if (results.empty())
    {
        std::cout << "No items found matching: " << query << std::endl;
        return;
    }
    
    // Display results
    std::cout << "Found " << results.size() << " item(s):" << std::endl;
    for (size_t i = 0; i < std::min(results.size(), size_t(10)); ++i)
    {
        std::cout << "  " << (i + 1) << ". " << results[i] << std::endl;
    }
    
    // Select item
    std::string slug;
    if (results.size() == 1)
    {
        slug = results[0];
        std::cout << "Selected: " << slug << std::endl;
    }
    else
    {
        size_t choice;
        std::cout << "Select item number (1-" << std::min(results.size(), size_t(10)) << "): ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        
        if (choice < 1 || choice > std::min(results.size(), size_t(10)))
        {
            std::cout << "Invalid choice" << std::endl;
            return;
        }
        slug = results[choice - 1];
    }
    
    // Get order details
    int platinum, quantity;
    bool visible;
    
    std::cout << "Enter platinum price: ";
    std::cin >> platinum;
    
    std::cout << "Enter quantity: ";
    std::cin >> quantity;
    
    std::cout << "Set visible? (1 = yes, 0 = no): ";
    int vis_input;
    std::cin >> vis_input;
    visible = (vis_input != 0);
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    
    // Create the order
    try
    {
        std::string orderId = session.createSellOrder(slug, platinum, quantity, visible);
        std::cout << "✓ Order created successfully!" << std::endl;
        std::cout << "  Order ID: " << orderId << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "✗ Failed to create order: " << e.what() << std::endl;
    }
}

void updateOrder(WarframeMarketAPI::AuthenticatedClient& session)
{
    std::cout << "\n--- Update Order ---" << std::endl;
    
    // Display current orders
    auto orders = session.getMyOrders();
    displayOrders(orders);
    
    if (orders.empty()) return;
    
    // Select order
    std::string orderId;
    std::cout << "\nEnter Order ID to update: ";
    std::getline(std::cin, orderId);
    
    // Get new values
    UpdateOrderRequest request;
    std::string input;
    
    std::cout << "Enter new platinum price (or press Enter to skip): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        request.hasPlatinum = true;
        request.platinum = std::stoi(input);
    }
    
    std::cout << "Enter new quantity (or press Enter to skip): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        request.hasQuantity = true;
        request.quantity = std::stoi(input);
    }
    
    std::cout << "Set visible? (1/0 or press Enter to skip): ";
    std::getline(std::cin, input);
    if (!input.empty())
    {
        request.hasVisible = true;
        request.visible = (std::stoi(input) != 0);
    }
    
    // Update the order
    try
    {
        session.updateOrder(orderId, request);
        std::cout << "✓ Order updated successfully!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "✗ Failed to update order: " << e.what() << std::endl;
    }
}

void closeOrder(WarframeMarketAPI::AuthenticatedClient& session)
{
    std::cout << "\n--- Close Order ---" << std::endl;
    
    // Display current orders
    auto orders = session.getMyOrders();
    displayOrders(orders);
    
    if (orders.empty()) return;
    
    // Select order
    std::string orderId;
    std::cout << "\nEnter Order ID to close: ";
    std::getline(std::cin, orderId);
    
    int quantity;
    std::cout << "Enter quantity sold: ";
    std::cin >> quantity;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    
    // Close the order
    try
    {
        session.closeOrder(orderId, quantity);
        std::cout << "✓ Order closed successfully!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "✗ Failed to close order: " << e.what() << std::endl;
    }
}

void deleteOrder(WarframeMarketAPI::AuthenticatedClient& session)
{
    std::cout << "\n--- Delete Order ---" << std::endl;
    
    // Display current orders
    auto orders = session.getMyOrders();
    displayOrders(orders);
    
    if (orders.empty()) return;
    
    // Select order
    std::string orderId;
    std::cout << "\nEnter Order ID to delete: ";
    std::getline(std::cin, orderId);
    
    // Confirm deletion
    std::cout << "Are you sure you want to delete this order? (yes/no): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    
    if (confirm != "yes")
    {
        std::cout << "Deletion cancelled" << std::endl;
        return;
    }
    
    // Delete the order
    try
    {
        session.deleteOrder(orderId);
        std::cout << "✓ Order deleted successfully!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "✗ Failed to delete order: " << e.what() << std::endl;
    }
}

int main()
{
    try
    {
        WarframeMarketAPI api;
        
        // ==================== Authentication ====================
        std::cout << "=== Warframe Market API - Order Management ===" << std::endl;
        std::cout << "\n--- Authentication ---" << std::endl;
        
        std::string email, password;
        std::cout << "Enter email: ";
        std::getline(std::cin, email);
        std::cout << "Enter password: ";
        std::getline(std::cin, password);
        
        auto auth = api.login(email, password);
        api.setJWT(auth.token);
        
        std::cout << "✓ Logged in as: " << auth.ingame_name << std::endl;

        auto session = api.authenticated();
        
        // ==================== Load Items ====================
        std::cout << "\n--- Loading Items Database ---" << std::endl;
        size_t itemCount = api.loadItems();
        std::cout << "✓ Loaded " << itemCount << " items" << std::endl;
        
        // ==================== Main Menu ====================
        while (true)
        {
            std::cout << "\n=== Order Management Menu ===" << std::endl;
            std::cout << "1. View my orders" << std::endl;
            std::cout << "2. Create sell order" << std::endl;
            std::cout << "3. Update order" << std::endl;
            std::cout << "4. Close order" << std::endl;
            std::cout << "5. Delete order" << std::endl;
            std::cout << "0. Exit" << std::endl;
            std::cout << "Choose option: ";
            
            int choice;
            std::cin >> choice;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            
            switch (choice)
            {
                case 1:
                {
                    auto orders = session.getMyOrders();
                    displayOrders(orders);
                    break;
                }
                case 2:
                    createOrder(api, session);
                    break;
                case 3:
                    updateOrder(session);
                    break;
                case 4:
                    closeOrder(session);
                    break;
                case 5:
                    deleteOrder(session);
                    break;
                case 0:
                    std::cout << "Goodbye!" << std::endl;
                    return 0;
                default:
                    std::cout << "Invalid option" << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
