#ifndef WFM_API_H
#define WFM_API_H

#include <string>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include "http_client.h"
#include "types.h"
#include "constants.h"

using json = nlohmann::json;

class WarframeMarketAPI
{
private:
    HttpClient http_client;
    std::string jwt;
    
    std::string extractBearerToken() const {
        return jwt.substr(4); // Remove "JWT " prefix
    }

public:
    // ==================== Authentication ====================
    
    /**
     * @brief Authenticate with Warframe.Market
     * @param email User email
     * @param password User password
     * @param device_id Optional device ID
     * @return AuthResponse containing token and user info
     */
    AuthResponse login(const std::string& email, const std::string& password, 
                       const std::string& device_id = "")
    {
        json payload = {
            {"auth_type", "header"},
            {"email", email},
            {"password", password},
            {"device_id", device_id}
        };

        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "JWT"}
        };

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
    

    void setJWT(const std::string& token) { jwt = token; }
    std::string getJWT() const { return jwt; }

    // ==================== Item Database ====================
    
    /**
     * @brief Load all tradeable items from Warframe.Market
     * @return Number of items loaded
     */
    size_t loadItems()
    {
        const std::string url = V2_API + "/items";
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json; utf-8"},
            {"Accept", "application/json"},
            {"auth_type", "header"},
            {"platform", "pc"},
            {"language", "en"},
            {"User-Agent", "wft/1.0"}
        };
        
        std::string response_body = http_client.Get(url, headers);
        json data = json::parse(response_body);
        
        if (!data.contains("data") || !data["data"].is_array()) {
            throw std::runtime_error("Invalid API response format");
        }
        
        if (data["data"].empty()) {
            throw std::runtime_error("API response contains no items");
        }
        
        for (const auto& dat : data["data"]) {
            std::string id = dat["id"].get<std::string>();
            std::string slug = dat["slug"].get<std::string>();
            
            itemDB.idToSlug[id] = slug;
            itemDB.slugToId[slug] = id;
            itemDB.allSlugs.push_back(slug);
        }
        
        return itemDB.size();
    }
    
    /**
     * @brief Search for items by name substring
     * @param query Search query
     * @return Vector of matching item slugs
     */
    std::vector<std::string> searchItems(const std::string& query) const
    {
        return itemDB.searchByName(query);
    }
    

    std::string getSlugByID(const std::string& id) const
    {
        return itemDB.getSlugFromId(id);
    }
    

    std::string getIDBySlug(const std::string& slug) const
    {
        return itemDB.getIdFromSlug(slug);
    }
    

    const std::vector<std::string>& getAllSlugs() const
    {
        return itemDB.allSlugs;
    }
    

    size_t getItemCount() const
    {
        return itemDB.size();
    }

    // ==================== Order Management ====================
    
    /**
     * @brief Get user's current orders
     * @return Vector of Order objects
     */
    std::vector<OrderDetail> getMyOrders()
    {
        std::string auther = extractBearerToken();
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}
        };
        
        std::string url = V2_API + "/orders/my";
        std::string response_body = http_client.Get(url, headers);
        json data = json::parse(response_body);
        
        if (!data.contains("data") || !data["data"].is_array()) {
            throw std::runtime_error("Invalid API response format");
        }
        
        std::vector<OrderDetail> orders;
        for (const auto& dat : data["data"]) {
            OrderDetail order;
            order.id = dat["id"].get<std::string>();
            order.orderType = dat["type"].get<std::string>();
            order.platinum = dat["platinum"].get<int>();
            order.quantity = dat["quantity"].get<int>();
            order.itemId = dat["itemId"].get<std::string>();
            order.itemSlug = getSlugByID(order.itemId);
            order.visible = dat.value("visible", true);
            
            orders.push_back(order);
        }
        
        return orders;
    }
    
    /**
     * @brief Create a new sell order
     * @param itemSlug Item slug to sell
     * @param platinum Price in platinum
     * @param quantity Number of items
     * @param visible Whether order is visible
     * @param rank Optional mod rank
     * @return Created order ID
     */
    std::string createSellOrder(const std::string& itemSlug, int platinum, 
                                int quantity, bool visible = true, 
                                std::optional<int> rank = std::nullopt)
    {
        std::string auther = extractBearerToken();
        std::string itemId = getIDBySlug(itemSlug);
        
        std::string url = V2_API + "/order/";
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}
        };
        
        json payload = {
            {"itemId", itemId},
            {"type", "sell"},
            {"platinum", platinum},
            {"quantity", quantity},
            {"visible", visible}
        };
        
        if (rank.has_value()) {
            payload["rank"] = rank.value();
        }
        
        std::map<std::string, std::string> response_headers;
        std::string data = http_client.post(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        
        if (!response_data["error"].is_null()) {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }
        
        return response_data["data"]["id"].get<std::string>();
    }
    
    /**
     * @brief Update an existing order
     * @param orderId Order ID to update
     * @param platinum Optional new price
     * @param quantity Optional new quantity
     * @param visible Optional visibility setting
     * @param rank Optional mod rank
     */
    void updateOrder(const std::string& orderId, 
                    std::optional<int> platinum = std::nullopt,
                    std::optional<int> quantity = std::nullopt,
                    std::optional<bool> visible = std::nullopt,
                    std::optional<int> rank = std::nullopt)
    {
        std::string auther = extractBearerToken();
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}
        };
        
        json payload;
        if (platinum.has_value()) payload["platinum"] = platinum.value();
        if (quantity.has_value()) payload["quantity"] = quantity.value();
        if (visible.has_value()) payload["visible"] = visible.value();
        if (rank.has_value()) payload["rank"] = rank.value();
        
        if (payload.empty()) {
            throw std::invalid_argument("No fields provided to update");
        }
        
        std::string url = V2_API + "/order/" + orderId;
        std::map<std::string, std::string> response_headers;
        std::string data = http_client.patch(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        
        if (!response_data["error"].is_null()) {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }
    }
    
    /**
     * @brief Close an order (mark as sold)
     * @param orderId Order ID to close
     * @param quantity Number of items sold
     */
    void closeOrder(const std::string& orderId, int quantity)
    {
        std::string auther = extractBearerToken();
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}
        };
        
        std::string url = V2_API + "/order/" + orderId + "/close";
        json payload = {{"quantity", quantity}};
        
        std::map<std::string, std::string> response_headers;
        std::string data = http_client.post(url, payload, headers, response_headers);
        json response_data = json::parse(data);
        
        if (!response_data["error"].is_null()) {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }
    }
    
    /**
     * @brief Delete an order
     * @param orderId Order ID to delete
     */
    void deleteOrder(const std::string& orderId)
    {
        std::string auther = extractBearerToken();
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"User-Agent", "wf-market-cpp/0.1.0"},
            {"Platform", "pc"},
            {"Language", "en"},
            {"Crossplay", "true"},
            {"Authorization", "Bearer " + auther}
        };
        
        std::string url = V2_API + "/order/" + orderId;
        long response_code = http_client.Delete(url, headers);
        
        if (response_code != 200) {
            throw std::runtime_error("Failed to delete order: HTTP " + std::to_string(response_code));
        }
    }
};

#endif // WFM_API_H
