#ifndef WFM_CLIENT_H
#define WFM_CLIENT_H

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "constants.h"
#include "http_client.h"
#include "types.h"
#include "wfm_api.h"

/**
 * @file wfm_client.h
 * @brief Interactive console client, kept as a demo of the library.
 *
 * WarframeMarketAPI is the API; this class is a menu-driven front end over it.
 * It exists so the examples have something to drive, and it is deliberately
 * thin: all request building, validation, and parsing happens in the API and
 * in order_validation.h.
 *
 * Prefer WarframeMarketAPI for anything new.
 */
class WfmClient
{
public:
    WfmClient() : api_(new HttpClient()) {}

    void setJWT(const std::string &jwt) { api_.setJWT(jwt); }
    std::string getJWT() const { return api_.getJWT(); }

    std::vector<std::string> findItemsByName(const std::string &input) const
    {
        const auto results = api_.searchItems(input);
        if (results.empty())
        {
            std::cout << "No items found matching: " << input << "\n";
            return results;
        }

        std::cout << "Found " << results.size() << " item(s) matching '" << input << "':\n";
        for (const auto &slug : results)
        {
            std::cout << "  - " << slug << "\n";
        }
        return results;
    }

    std::string getSlugByID(const std::string &id) const { return api_.getSlugByID(id); }
    std::string getIDBySlug(const std::string &slug) const { return api_.getIDBySlug(slug); }
    const std::vector<std::string> &getAllSlugs() const { return api_.getAllSlugs(); }
    size_t getItemCount() const { return api_.getItemCount(); }

    AuthResponse login(const std::string &email, const std::string &password,
                       const std::string &device_id)
    {
        return api_.login(email, password, device_id);
    }

    void loadItems()
    {
        const size_t count = api_.loadItems();
        std::cout << "Loaded " << count << " items into database.\n";
    }

    void myOrders()
    {
        try
        {
            const auto orders = api_.authenticated().getMyOrders();
            if (orders.empty())
            {
                std::cout << "No orders found.\n";
                return;
            }

            for (const auto &order : orders)
            {
                std::cout << "Trade ID: " << order.id << "\n"
                          << "Type: " << order.orderType << "\n"
                          << "Platinum: " << order.platinum << "\n"
                          << "Quantity: " << order.quantity << "\n"
                          << "Item: " << order.itemSlug << "\n\n";
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error fetching orders: " << e.what() << "\n";
        }
    }

    void deleteOrder(const std::string &id)
    {
        try
        {
            api_.authenticated().deleteOrder(id);
            std::cout << "Order with ID: " << id << " deleted.\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to delete order " << id << ": " << e.what() << "\n";
        }
    }

    void addOrderSell()
    {
        std::cout << "Search for the slug you want to sell: \n";
        std::string query;
        std::cin >> query;

        const auto matches = findItemsByName(query);
        if (matches.empty())
        {
            std::cerr << "No matching items found for: " << query << "\n";
            return;
        }

        std::string slug = matches[0];
        if (matches.size() > 1)
        {
            std::cout << "Multiple matches found. Enter the slug you want to sell: ";
            std::cin >> slug;
        }

        // Only the fields the item requires are prompted for; the API layer
        // rejects inapplicable ones, so asking for everything would be noise.
        PlaceOrderRequest request;
        request.itemSlug = slug;

        std::cout << "Enter platinum price: ";
        std::cin >> request.platinum;
        std::cout << "Enter quantity: ";
        std::cin >> request.quantity;

        int visible_input = 0;
        std::cout << "Set visible? (1 for true, 0 for false): ";
        std::cin >> visible_input;
        request.visible = (visible_input != 0);

        try
        {
            const Item &item = api_.getItem(slug);
            if (item.requiresRank())
            {
                std::cout << "Enter rank (0-" << item.maxRank << "): ";
                std::cin >> request.rank;
                request.hasRank = true;
            }
            if (item.requiresSubtype())
            {
                std::cout << "Enter subtype (";
                for (size_t i = 0; i < item.subtypes.size(); ++i)
                {
                    std::cout << (i ? ", " : "") << item.subtypes[i];
                }
                std::cout << "): ";
                std::cin >> request.subtype;
                request.hasSubtype = true;
            }
            if (item.requiresPerTrade())
            {
                std::cout << "Enter perTrade (1-6): ";
                std::cin >> request.perTrade;
                request.hasPerTrade = true;
            }
            if (item.hasMaxAmberStars && item.maxAmberStars > 0)
            {
                std::cout << "Enter amber stars (0-" << item.maxAmberStars << "): ";
                std::cin >> request.amberStars;
                request.hasAmberStars = true;
            }
            if (item.hasMaxCyanStars && item.maxCyanStars > 0)
            {
                std::cout << "Enter cyan stars (0-" << item.maxCyanStars << "): ";
                std::cin >> request.cyanStars;
                request.hasCyanStars = true;
            }

            const std::string orderId = api_.authenticated().placeOrder(request);
            std::cout << "Order created: " << orderId << "\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to create order: " << e.what() << "\n";
        }
    }

    void closeOrder(const int amount)
    {
        myOrders();
        std::cout << "Which would you like to close? \n";
        std::string id;
        std::cin >> id;

        try
        {
            const Transaction tx = api_.authenticated().closeOrder(id, amount);
            std::cout << "Closed " << amount << " unit(s); transaction " << tx.id << "\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error closing order: " << e.what() << "\n";
        }
    }

    void patchOrder()
    {
        myOrders();
        std::cout << "Copy the order id: \n";
        std::string orderId;
        std::cin >> orderId;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        UpdateOrderRequest request;
        std::string input;

        std::cout << "Enter platinum price (or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
        {
            request.hasPlatinum = true;
            request.platinum = std::stoi(input);
        }

        std::cout << "Enter quantity (or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
        {
            request.hasQuantity = true;
            request.quantity = std::stoi(input);
        }

        std::cout << "Set visible? (1/0 or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
        {
            request.hasVisible = true;
            request.visible = (std::stoi(input) != 0);
        }

        try
        {
            api_.authenticated().updateOrder(orderId, request);
            std::cout << "Order updated.\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error updating order: " << e.what() << "\n";
        }
    }

private:
    WarframeMarketAPI api_;
};

#endif // WFM_CLIENT_H
