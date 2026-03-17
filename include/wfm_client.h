#ifndef WFM_CLIENT_H
#define WFM_CLIENT_H

#include <string>
#include <optional>
#include <iostream>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include "http_client.h"
#include "types.h"
#include "constants.h"

using json = nlohmann::json;

class WfmClient
{
private:
    HttpClient http_client;
    std::string jwt;

public:
    // JWT management
    void setJWT(std::string JWT) { jwt = JWT; }
    std::string getJWT() { return jwt; }

    // Search for items by name substring
    std::vector<std::string> findItemsByName(const std::string &input) const
    {

        auto results = itemDB.searchByName(input);
        if (results.empty())
        {
            std::cout << "No items found matching: " << input << "\n";
        }
        else
        {
            std::cout << "Found " << results.size() << " item(s) matching '" << input << "':" << "\n";
            for (const auto &slug : results)
                std::cout << "  - " << slug << "\n";
        }
        return results;
    }

    // Get slug from item ID
    std::string getSlugByID(const std::string &id) const
    {
        return itemDB.getSlugFromId(id);
    }

    // Get ID from item slug
    std::string getIDBySlug(const std::string &slug) const
    {
        return itemDB.getIdFromSlug(slug);
    }

    // Get all loaded item slugs
    const std::vector<std::string> &getAllSlugs() const
    {
        return itemDB.allSlugs;
    }

    // Get number of loaded items
    size_t getItemCount() const
    {
        return itemDB.size();
    }

    AuthResponse login(const std::string &username, const std::string &password, const std::string &device_id)
    {
        json payload = {
            {"auth_type", "header"},
            {"email", username},
            {"password", password},
            {"device_id", device_id}};

        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "JWT"}};

        std::map<std::string, std::string> response_headers;
        std::string url = V1_API + "/auth/signin";
        std::string response_body = http_client.post(url, payload, headers, response_headers);

        json data = json::parse(response_body);
        std::string token = response_headers["authorization"];

        AuthResponse result;

        result.token = token;
        result.user_id = data["payload"]["user"]["id"];
        result.ingame_name = data["payload"]["user"]["ingame_name"];

        return result;
    }
    /*
    Item getItemStats(const std::string &itemSlug)
    {
        //keep for ref
        std::map<std::string, std::string> header = {
            {"Content-Type", "application/json; utf-8"},
            {"Accept", "application/json"},
            {"auth_type", "header"},
            {"platform", "pc"},
            {"language", "en"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
        };


        noAuthHeader header;
        std::string url = V1_API + "/items/" + itemSlug + "/statistics";
        std::string response = http_client.Get(url, header);
        json data = json::parse(response);

        Item item;
        item.slug = itemSlug;

        if (data.contains("payload") && data["payload"].contains("statistics_live"))
        {
            auto live_stats = data["payload"]["statistics_live"]["48hours"];

            if (!live_stats.empty())
            {
                // Get the most recent entry (index 0 is most recent)
                double total_volume = 0.0;
                double min_price = std::numeric_limits<double>::max();

                // Get only sell volume (index 1)
                if (live_stats.size() > 1 && live_stats[1].contains("volume"))
                {
                    total_volume = live_stats[1]["volume"].get<double>();
                }

                // Get minimum sell price from sell orders (second entry, index 1)
                if (live_stats.size() > 1 && live_stats[1].contains("min_price"))
                {
                    min_price = live_stats[1]["min_price"].get<double>();
                }

                item.avgVolume48h = total_volume;
                item.avgMinPrice48h = min_price;
            }
        }

        return item;
    }
    */
    void myOrders()
    {
        try
        {
            std::string auther = jwt.substr(4);
            std::map<std::string, std::string> headers = {
                {"Content-Type", "application/json"},
                {"Accept", "application/json"},
                {"User-Agent", "wf-market-cpp/0.1.0"},
                {"Platform", "pc"},
                {"Language", "en"},
                {"Crossplay", "true"},
                {"Authorization", "Bearer " + auther}};

            std::string url = V2_API + "/orders/my";
            std::string response_body = http_client.Get(url, headers);
            json data = json::parse(response_body);

            if (!data.contains("data") || !data["data"].is_array())
            {
                std::cerr << "Invalid API response format" << "\n";
                return;
            }

            if (data["data"].empty())
            {
                std::cout << "No orders found." << "\n";
                return;
            }

            for (const auto &dat : data["data"])
            {

                std::string toSlug = dat["itemId"].get<std::string>();
                std::string slug = getSlugByID(toSlug);

                std::cout << "Trade ID: " << dat["id"].get<std::string>() << "\nType: " << dat["type"].get<std::string>() << "\nPlatinum: " << dat["platinum"].get<std::int32_t>() << "\nQuantity: " << dat["quantity"].get<std::int32_t>() << "\nItem: " << slug << "\n"
                          << "\n";
            }
        }
        catch (const json::parse_error &e)
        {
            std::cerr << "JSON parsing error: " << e.what() << "\n";
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error fetching orders: " << e.what() << "\n";
        }
    }
    void loadItems()
    {
        const std::string url = V2_API + "/items";
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json; utf-8"},
            {"Accept", "application/json"},
            {"auth_type", "header"},
            {"platform", "pc"},
            {"language", "en"},
            {"User-Agent", "wft/1.0"}};
        std::string response_body = http_client.Get(url, headers);
        json data = json::parse(response_body);
        if (!data.contains("data") || !data["data"].is_array())
        {
            std::cerr << "Invalid API response format" << "\n";
            return;
        }

        if (data["data"].empty())
        {
            std::cout << "API reponse malformed. Error from API:" << data["error"] << "\n";
            return;
        }
        for (const auto &dat : data["data"])
        {
            std::string id = dat["id"].get<std::string>();
            std::string slug = dat["slug"].get<std::string>();

            itemDB.idToSlug[id] = slug;
            itemDB.slugToId[slug] = id;
            itemDB.allSlugs.push_back(slug);
        }

        std::cout << "Loaded " << itemDB.size() << " items into database." << "\n";
    }
    void deleteOrder(const std::string id)
    {
        std::string auther = jwt.substr(4);
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}

        };
        std::string url = V2_API + "/order/" + id;
        long response_body = http_client.Delete(url, headers);
        if (response_body == 200)
        {
            std::cout << "Order with ID: " << id << " deleted." << "\n";
        }
        else
        {
            std::cerr << "No Orders with ID: " << id << "\n";
        }
    }
    void addOrderSell()
    {
        std::string auther = jwt.substr(4);

        std::string url = V2_API + "/order/";
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}};

        std::cout << "Search for the Slug you want to sell: " << "\n";
        std::string name;
        std::cin >> name;
        auto matches = findItemsByName(name);
        if (matches.empty())
        {
            std::cerr << "No matching items found for: " << name << "\n";
            return;
        }

        std::string slug;
        if (matches.size() == 1)
        {
            slug = matches[0];
        }
        else
        {
            std::cout << "Multiple matches found. Enter the slug you want to sell: ";
            std::cin >> slug;
        }
        std::string id = slug2id(slug);
        int plat, quantity;
        bool visibility;
        std::optional<int> rank;
        std::cout << "Enter platinum price: ";
        std::cin >> plat;
        std::cout << "Enter quantity: ";
        std::cin >> quantity;
        std::cout << "Set visible? (1 for true, 0 for false): ";
        int vis_input;
        std::cin >> vis_input;
        visibility = (vis_input != 0);
        std::cout << "Enter rank (or leave empty to skip): ";
        std::string rank_input;
        std::getline(std::cin, rank_input);
        if (!rank_input.empty())
            rank = std::stoi(rank_input);
        json payload = {
            {"itemId", id},
            {"type", "sell"},
            {"platinum", plat},
            {"quantity", quantity},
            {"visible", visibility}};
        if (rank.has_value())
        {
            payload["rank"] = rank.value();
        }
        std::map<std::string, std::string> response_headers;
        std::string data = http_client.post(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        if (response_data["error"].is_null())
        {
            std::cout << "Order created." << "\n";
        }
        else
        {
            std::cout << response_data["error"].get<std::string>() << "\n";
        }
    }
    void closeOrder(const int amt)
    {
        std::string auther = jwt.substr(4);

        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}};

        myOrders();
        std::cout << "Which would u like to close? " << "\n";
        std::string id;
        std::cin >> id;

        std::string url = V2_API + "/order/" + id + "/close";
        json payload = {
            {"quantity", amt}};
        std::map<std::string, std::string> response_headers;
        std::string data = http_client.post(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        if (response_data["error"].is_null())
        {
            std::cout << "You have closed " << amt << " order(s):" << "\n";
        }
        else
        {
            std::cout << "Error wfm response: " << response_data["error"].get<std::string>() << "\n";
        }
    }
    void patchOrder()
    {
        std::string auther = jwt.substr(4);
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}};
        myOrders();
        std::cout << "Copy the order: " << "\n";
        std::string orderId;
        std::cin >> orderId;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Clear input buffer
        std::optional<int> plat = std::nullopt;
        std::optional<int> quantity = std::nullopt;
        std::optional<int> rank = std::nullopt;
        std::optional<bool> visibility = std::nullopt;

        std::string input;
        std::cout << "Enter platinum price (or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
            plat = std::stoi(input);

        std::cout << "Enter quantity (or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
            quantity = std::stoi(input);

        std::cout << "Enter rank (or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
            rank = std::stoi(input);

        std::cout << "Set visible? (1 for true, 0 for false, or leave empty to skip): ";
        std::getline(std::cin, input);
        if (!input.empty())
            visibility = (std::stoi(input) != 0);

        if (!plat.has_value() && !quantity.has_value() && !visibility.has_value() && !rank.has_value())
        {
            std::cerr << "No fields provided to update." << "\n";
            return;
        }

        std::string url = V2_API + "/order/" + orderId;

        json payload;
        if (plat.has_value())
        {
            payload["platinum"] = plat.value();
        }
        if (quantity.has_value())
        {
            payload["quantity"] = quantity.value();
        }
        if (visibility.has_value())
        {
            payload["visible"] = visibility.value();
        }
        if (rank.has_value())
        {
            payload["rank"] = rank.value();
        }

        std::map<std::string, std::string> response_headers;

        std::string data = http_client.patch(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        if (response_data["error"].is_null())
        {
            std::cout << "Order updated." << "\n";
        }
        else
        {
            std::cout << "Error wfm response: " << response_data["error"].get<std::string>() << "\n";
        }
    }

private:
    std::string slug2id(const std::string &slug)
    {
        return itemDB.getIdFromSlug(slug);
    }
};

#endif // WFM_CLIENT_H
