#ifndef WFM_API_H
#define WFM_API_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "http_client.h"
#include "http_transport.h"
#include "types.h"
#include "constants.h"

using json = nlohmann::json;

class WarframeMarketAPI
{
private:
    class AuthState
    {
    public:
        virtual ~AuthState() {}
        virtual bool isAuthenticated() const = 0;
    };

    class UnauthenticatedState : public AuthState
    {
    public:
        bool isAuthenticated() const override { return false; }
    };

    class AuthenticatedState : public AuthState
    {
    public:
        bool isAuthenticated() const override { return true; }
    };

    struct HeaderFactory
    {
        static std::map<std::string, std::string> commonJsonHeaders()
        {
            return {
                {"Content-Type", "application/json"},
                {"Accept", "application/json"},
                {"User-Agent", "wf-market-cpp/0.1.0"},
                {"Platform", "pc"},
                {"Language", "en"},
                {"Crossplay", "true"}
            };
        }

        static std::map<std::string, std::string> loginHeaders()
        {
            return {
                {"Content-Type", "application/json"},
                {"Authorization", "JWT"}
            };
        }

        static std::map<std::string, std::string> publicHeaders()
        {
            std::map<std::string, std::string> headers = commonJsonHeaders();
            headers["auth_type"] = "header";
            return headers;
        }

        static std::map<std::string, std::string> authHeaders(const std::string &bearerToken)
        {
            std::map<std::string, std::string> headers = commonJsonHeaders();
            headers["Authorization"] = "Bearer " + bearerToken;
            return headers;
        }
    };

    std::unique_ptr<HttpTransport> http_client;
    std::string jwt;
    std::unique_ptr<AuthState> state;

    std::string extractBearerToken() const
    {
        const std::string prefix = "JWT ";
        if (jwt.size() <= prefix.size() || jwt.compare(0, prefix.size(), prefix) != 0)
        {
            throw std::runtime_error("Invalid JWT format. Expected token with 'JWT ' prefix.");
        }
        return jwt.substr(prefix.size());
    }

    void ensureAuthenticated() const
    {
        if (!state || !state->isAuthenticated())
        {
            throw std::runtime_error("Authentication required. Call login() and setJWT() first.");
        }
    }

    std::vector<OrderDetail> getMyOrdersImpl()
    {
        const std::string auther = extractBearerToken();
        const std::map<std::string, std::string> headers = HeaderFactory::authHeaders(auther);

        const std::string url = V2_API + "/orders/my";
        const std::string response_body = http_client->Get(url, headers);
        json data = json::parse(response_body);

        if (!data.contains("data") || !data["data"].is_array())
        {
            throw std::runtime_error("Invalid API response format");
        }

        std::vector<OrderDetail> orders;
        for (const auto &dat : data["data"])
        {
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

    std::string createSellOrderImpl(const SellOrderRequest &request)
    {
        const std::string auther = extractBearerToken();
        const std::string itemId = getIDBySlug(request.itemSlug);
        const std::map<std::string, std::string> headers = HeaderFactory::authHeaders(auther);

        json payload = {
            {"itemId", itemId},
            {"type", "sell"},
            {"platinum", request.platinum},
            {"quantity", request.quantity},
            {"visible", request.visible}
        };

        if (request.hasRank)
        {
            payload["rank"] = request.rank;
        }

        std::map<std::string, std::string> response_headers;
        const std::string url = V2_API + "/order/";
        const std::string data = http_client->post(url, payload, headers, response_headers);
        json response_data = json::parse(data);

        if (!response_data["error"].is_null())
        {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }

        return response_data["data"]["id"].get<std::string>();
    }

    void updateOrderImpl(const std::string &orderId, const UpdateOrderRequest &request)
    {
        if (!request.hasChanges())
        {
            throw std::invalid_argument("No fields provided to update");
        }

        const std::string auther = extractBearerToken();
        const std::map<std::string, std::string> headers = HeaderFactory::authHeaders(auther);

        json payload;
        if (request.hasPlatinum)
        {
            payload["platinum"] = request.platinum;
        }
        if (request.hasQuantity)
        {
            payload["quantity"] = request.quantity;
        }
        if (request.hasVisible)
        {
            payload["visible"] = request.visible;
        }
        if (request.hasRank)
        {
            payload["rank"] = request.rank;
        }

        const std::string url = V2_API + "/order/" + orderId;
        std::map<std::string, std::string> response_headers;
        const std::string data = http_client->patch(url, payload, headers, response_headers);
        json response_data = json::parse(data);

        if (!response_data["error"].is_null())
        {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }
    }

    void closeOrderImpl(const std::string &orderId, int quantity)
    {
        const std::string auther = extractBearerToken();
        const std::map<std::string, std::string> headers = HeaderFactory::authHeaders(auther);

        const std::string url = V2_API + "/order/" + orderId + "/close";
        const json payload = {{"quantity", quantity}};

        std::map<std::string, std::string> response_headers;
        const std::string data = http_client->post(url, payload, headers, response_headers);
        json response_data = json::parse(data);

        if (!response_data["error"].is_null())
        {
            throw std::runtime_error("API error: " + response_data["error"].get<std::string>());
        }
    }

    void deleteOrderImpl(const std::string &orderId)
    {
        const std::string auther = extractBearerToken();
        const std::map<std::string, std::string> headers = HeaderFactory::authHeaders(auther);

        const std::string url = V2_API + "/order/" + orderId;
        const long response_code = http_client->Delete(url, headers);

        if (response_code != 200)
        {
            throw std::runtime_error("Failed to delete order: HTTP " + std::to_string(response_code));
        }
    }

public:
    WarframeMarketAPI()
        : http_client(new HttpClient()), state(new UnauthenticatedState()) {}

    /**
     * @brief Construct with an alternate transport (tests, proxies, stubs).
     * @param transport Non-null transport; ownership is transferred.
     */
    explicit WarframeMarketAPI(std::unique_ptr<HttpTransport> transport)
        : http_client(std::move(transport)), state(new UnauthenticatedState())
    {
        if (!http_client)
        {
            throw std::invalid_argument("WarframeMarketAPI requires a transport");
        }
    }

    class AuthenticatedClient
    {
    public:
        explicit AuthenticatedClient(WarframeMarketAPI &apiRef) : api(apiRef) {}

        std::vector<OrderDetail> getMyOrders()
        {
            api.ensureAuthenticated();
            return api.getMyOrdersImpl();
        }

        std::string createSellOrder(const SellOrderRequest &request)
        {
            api.ensureAuthenticated();
            return api.createSellOrderImpl(request);
        }

        std::string createSellOrder(const std::string &itemSlug, int platinum, int quantity, bool visible)
        {
            SellOrderRequest request;
            request.itemSlug = itemSlug;
            request.platinum = platinum;
            request.quantity = quantity;
            request.visible = visible;
            return createSellOrder(request);
        }

        void updateOrder(const std::string &orderId, const UpdateOrderRequest &request)
        {
            api.ensureAuthenticated();
            api.updateOrderImpl(orderId, request);
        }

        void closeOrder(const std::string &orderId, int quantity)
        {
            api.ensureAuthenticated();
            api.closeOrderImpl(orderId, quantity);
        }

        void deleteOrder(const std::string &orderId)
        {
            api.ensureAuthenticated();
            api.deleteOrderImpl(orderId);
        }

    private:
        WarframeMarketAPI &api;
    };

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

        std::map<std::string, std::string> headers = HeaderFactory::loginHeaders();

        std::map<std::string, std::string> response_headers;
        std::string url = V1_API + "/auth/signin";
        std::string response_body = http_client->post(url, payload, headers, response_headers);

        json data = json::parse(response_body);
        std::string token = response_headers["authorization"];

        AuthResponse result;
        result.token = token;
        result.user_id = data["payload"]["user"]["id"];
        result.ingame_name = data["payload"]["user"]["ingame_name"];

        return result;
    }
    

    void setJWT(const std::string& token)
    {
        jwt = token;
        if (jwt.empty())
        {
            state.reset(new UnauthenticatedState());
        }
        else
        {
            state.reset(new AuthenticatedState());
        }
    }

    void clearJWT()
    {
        jwt.clear();
        state.reset(new UnauthenticatedState());
    }

    std::string getJWT() const { return jwt; }
    bool isAuthenticated() const { return state && state->isAuthenticated(); }

    AuthenticatedClient authenticated()
    {
        ensureAuthenticated();
        return AuthenticatedClient(*this);
    }

    // ==================== Item Database ====================
    
    /**
     * @brief Load all tradeable items from Warframe.Market
     * @return Number of items loaded
     */
    size_t loadItems()
    {
        const std::string url = V2_API + "/items";
        std::map<std::string, std::string> headers = HeaderFactory::publicHeaders();
        headers["Content-Type"] = "application/json; utf-8";
        
        std::string response_body = http_client->Get(url, headers);
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
};

#endif // WFM_API_H
