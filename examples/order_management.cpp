/**
 * @file order_management.cpp
 * @brief Interactive order management example.
 *
 * Demonstrates the order lifecycle against the v2 API using the library's own
 * request validation. Item metadata decides which fields an order needs, so
 * the prompts are driven by `requires*()` rather than asking every caller for
 * every field.
 */

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "../include/wfm_api.h"

namespace
{

void displayOrders(const std::vector<Order> &orders)
{
    if (orders.empty())
    {
        std::cout << "No orders found." << std::endl;
        return;
    }

    std::cout << "\n-----------------------------------------------------------------\n";
    std::cout << "                        Current Orders\n";
    std::cout << "-----------------------------------------------------------------\n";

    for (size_t i = 0; i < orders.size(); ++i)
    {
        const Order &order = orders[i];
        std::cout << "Order #" << (i + 1) << "\n";
        std::cout << "  ID:       " << order.id << "\n";
        std::cout << "  Type:     " << order.orderType << "\n";
        std::cout << "  Item:     " << order.itemSlug << "\n";
        std::cout << "  Price:    " << order.platinum << " platinum";
        if (order.hasPerTrade && order.perTrade > 0)
        {
            std::cout << " per trade of " << order.perTrade;
        }
        std::cout << "\n";
        std::cout << "  Quantity: " << order.quantity << "\n";
        std::cout << "  Visible:  " << (order.visible ? "Yes" : "No") << "\n";
        if (order.hasRank)
        {
            std::cout << "  Rank:     " << order.rank << "\n";
        }
        if (order.hasSubtype)
        {
            std::cout << "  Subtype:  " << order.subtype << "\n";
        }
        if (i + 1 < orders.size())
        {
            std::cout << "-----------------------------------------------------------------\n";
        }
    }
    std::cout << "-----------------------------------------------------------------\n";
}

/// Prompt for a value, leaving it unset when the user submits an empty line.
bool promptOptionalInt(const std::string &label, int &out)
{
    std::cout << label << " (or press Enter to skip): ";
    std::string input;
    std::getline(std::cin, input);
    if (input.empty())
    {
        return false;
    }
    out = std::stoi(input);
    return true;
}

void createOrder(WarframeMarketAPI &api, WarframeMarketAPI::AuthenticatedClient &session)
{
    std::cout << "\n--- Create Order ---\n";

    std::string query;
    std::cout << "Enter item name to search: ";
    std::getline(std::cin, query);

    const std::vector<std::string> results = api.searchItems(query);
    if (results.empty())
    {
        std::cout << "No items found matching: " << query << "\n";
        return;
    }

    const size_t limit = std::min(results.size(), size_t(10));
    std::cout << "Found " << results.size() << " item(s):\n";
    for (size_t i = 0; i < limit; ++i)
    {
        const Item &item = api.getItem(results[i]);
        std::cout << "  " << (i + 1) << ". " << results[i];
        if (!item.itemName.empty())
        {
            std::cout << "  (" << item.itemName << ")";
        }
        std::cout << "\n";
    }

    std::string slug = results[0];
    if (limit > 1)
    {
        size_t choice = 0;
        std::cout << "Select item number (1-" << limit << "): ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (choice < 1 || choice > limit)
        {
            std::cout << "Invalid choice" << std::endl;
            return;
        }
        slug = results[choice - 1];
    }

    PlaceOrderRequest request;
    request.itemSlug = slug;

    std::cout << "Enter platinum price: ";
    std::cin >> request.platinum;
    std::cout << "Enter quantity: ";
    std::cin >> request.quantity;

    int visible = 1;
    std::cout << "Set visible? (1 = yes, 0 = no): ";
    std::cin >> visible;
    request.visible = (visible != 0);
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // The item decides which fields are required, so ask only for those. The
    // API layer rejects anything inapplicable before the request is sent.
    const Item &item = api.getItem(slug);
    if (item.requiresRank())
    {
        std::cout << "Enter rank (0-" << item.maxRank << "): ";
        std::cin >> request.rank;
        request.hasRank = true;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    if (item.requiresSubtype())
    {
        std::cout << "Available subtypes:";
        for (const auto &subtype : item.subtypes)
        {
            std::cout << " " << subtype;
        }
        std::cout << "\nEnter subtype: ";
        std::getline(std::cin, request.subtype);
        request.hasSubtype = true;
    }
    if (item.requiresPerTrade())
    {
        std::cout << "This item trades in bulk. Enter perTrade (1-6): ";
        std::cin >> request.perTrade;
        request.hasPerTrade = true;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    if (item.hasMaxAmberStars && item.maxAmberStars > 0)
    {
        std::cout << "Enter amber stars (0-" << item.maxAmberStars << "): ";
        std::cin >> request.amberStars;
        request.hasAmberStars = true;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    if (item.hasMaxCyanStars && item.maxCyanStars > 0)
    {
        std::cout << "Enter cyan stars (0-" << item.maxCyanStars << "): ";
        std::cin >> request.cyanStars;
        request.hasCyanStars = true;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    try
    {
        const std::string orderId = session.placeOrder(request);
        std::cout << "Order created successfully. Order ID: " << orderId << "\n";
    }
    catch (const order_validation::ValidationError &e)
    {
        std::cout << "Request rejected for field '" << e.field() << "': " << e.what() << "\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to create order: " << e.what() << "\n";
    }
}

void updateOrder(WarframeMarketAPI::AuthenticatedClient &session)
{
    std::cout << "\n--- Update Order ---\n";

    const std::vector<Order> orders = session.getMyOrders();
    displayOrders(orders);
    if (orders.empty())
    {
        return;
    }

    std::string orderId;
    std::cout << "\nEnter Order ID to update: ";
    std::getline(std::cin, orderId);

    UpdateOrderRequest request;
    int value = 0;
    if (promptOptionalInt("Enter new platinum price", value))
    {
        request.hasPlatinum = true;
        request.platinum = value;
    }
    if (promptOptionalInt("Enter new quantity", value))
    {
        request.hasQuantity = true;
        request.quantity = value;
    }

    std::cout << "Set visible? (1/0 or press Enter to skip): ";
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty())
    {
        request.hasVisible = true;
        request.visible = (std::stoi(input) != 0);
    }

    try
    {
        session.updateOrder(orderId, request);
        std::cout << "Order updated successfully.\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to update order: " << e.what() << "\n";
    }
}

void closeOrder(WarframeMarketAPI::AuthenticatedClient &session)
{
    std::cout << "\n--- Close Order ---\n";

    const std::vector<Order> orders = session.getMyOrders();
    displayOrders(orders);
    if (orders.empty())
    {
        return;
    }

    std::string orderId;
    std::cout << "\nEnter Order ID to close: ";
    std::getline(std::cin, orderId);

    int quantity = 0;
    std::cout << "Enter quantity sold: ";
    std::cin >> quantity;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    try
    {
        const Transaction tx = session.closeOrder(orderId, quantity);
        std::cout << "Closed " << quantity << " unit(s). Transaction ID: " << tx.id << "\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to close order: " << e.what() << "\n";
    }
}

void deleteOrder(WarframeMarketAPI::AuthenticatedClient &session)
{
    std::cout << "\n--- Delete Order ---\n";

    const std::vector<Order> orders = session.getMyOrders();
    displayOrders(orders);
    if (orders.empty())
    {
        return;
    }

    std::string orderId;
    std::cout << "\nEnter Order ID to delete: ";
    std::getline(std::cin, orderId);

    std::cout << "Are you sure you want to delete this order? (yes/no): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    if (confirm != "yes")
    {
        std::cout << "Deletion cancelled\n";
        return;
    }

    try
    {
        session.deleteOrder(orderId);
        std::cout << "Order deleted successfully.\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to delete order: " << e.what() << "\n";
    }
}

} // namespace

int main()
{
    try
    {
        WarframeMarketAPI api;

        std::cout << "=== Warframe Market API - Order Management ===\n";
        std::cout << "\n--- Authentication ---\n";

        std::string email;
        std::string password;
        std::cout << "Enter email: ";
        std::getline(std::cin, email);
        std::cout << "Enter password: ";
        std::getline(std::cin, password);

        const AuthResponse auth = api.login(email, password);
        api.setJWT(auth.token);
        std::cout << "Logged in as: " << auth.ingame_name << "\n";

        WarframeMarketAPI::AuthenticatedClient session = api.authenticated();

        std::cout << "\n--- Loading Items Database ---\n";
        std::cout << "Loaded " << api.loadItems() << " items\n";

        while (true)
        {
            std::cout << "\n=== Order Management Menu ===\n";
            std::cout << "1. View my orders\n";
            std::cout << "2. Create order\n";
            std::cout << "3. Update order\n";
            std::cout << "4. Close order\n";
            std::cout << "5. Delete order\n";
            std::cout << "0. Exit\n";
            std::cout << "Choose option: ";

            int choice = 0;
            std::cin >> choice;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            switch (choice)
            {
            case 1:
                displayOrders(session.getMyOrders());
                break;
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
                std::cout << "Goodbye!\n";
                return 0;
            default:
                std::cout << "Invalid option\n";
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
