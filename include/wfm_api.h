#ifndef WFM_API_H
#define WFM_API_H

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "api_error.h"
#include "constants.h"
#include "http_client.h"
#include "http_transport.h"
#include "order_validation.h"
#include "types.h"
#include "wfm_json.h"

/**
 * @file wfm_api.h
 * @brief Main entry point for the Warframe.Market API client.
 *
 * The v2 API is the supported surface: v1 REST data endpoints are gone, and
 * v2 rejects requests that do not match an item's metadata. Order calls
 * therefore validate against the loaded item database before sending, so a
 * malformed request fails locally with a useful message instead of costing a
 * round trip and returning a terse error code.
 *
 * Everything here runs offline against a substituted HttpTransport, which is
 * how the test suite covers request/parse behaviour without credentials.
 */
class WarframeMarketAPI
{
private:
    /**
     * @brief Auth state.
     *
     * Kept as an explicit state object so that order methods can be refused
     * before any transport work happens, rather than failing with a 401 that
     * looks like a server problem.
     */
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

    /**
     * @brief Builds request headers.
     *
     * The `User-Agent` identifies this library because the API rules require
     * clients to be identifiable, and `Platform`/`Crossplay` are needed for
     * the endpoints whose results depend on the marketplace shard.
     */
    struct HeaderFactory
    {
        static std::string userAgent()
        {
            return WFT_USER_AGENT;
        }

        static std::map<std::string, std::string, CaseInsensitiveLess> commonHeaders()
        {
            std::map<std::string, std::string, CaseInsensitiveLess> headers;
            headers["Content-Type"] = "application/json";
            headers["Accept"] = "application/json";
            headers["User-Agent"] = userAgent();
            headers["Platform"] = "pc";
            headers["Crossplay"] = "true";
            return headers;
        }

        static std::map<std::string, std::string, CaseInsensitiveLess> publicHeaders()
        {
            return commonHeaders();
        }

        static std::map<std::string, std::string, CaseInsensitiveLess>
        authHeaders(const std::string &accessToken)
        {
            auto headers = commonHeaders();
            headers["Authorization"] = "Bearer " + accessToken;
            return headers;
        }
    };

    std::unique_ptr<HttpTransport> http_client;
    std::string accessToken;
    std::unique_ptr<AuthState> state;
    std::string platform = "pc";
    std::string language = "en";

    void ensureAuthenticated() const
    {
        if (!state || !state->isAuthenticated())
        {
            throw std::runtime_error(
                "Authentication required. Call login() or setJWT() first.");
        }
    }

    /// Convert the stored token to the header form, tolerating a "JWT " prefix.
    std::string bearerToken() const
    {
        if (accessToken.empty())
        {
            throw std::runtime_error("No access token set");
        }

        // Tokens handed to setJWT() may still carry the legacy "JWT " prefix.
        static const std::string legacy_prefix = "JWT ";
        if (accessToken.compare(0, legacy_prefix.size(), legacy_prefix) == 0)
        {
            return accessToken.substr(legacy_prefix.size());
        }
        return accessToken;
    }

    /// Headers for the marketplace-affecting endpoints.
    HttpTransport::Headers contextHeaders() const
    {
        auto headers = HeaderFactory::commonHeaders();
        headers["Platform"] = platform;
        headers["Language"] = language;
        return headers;
    }

    // ==================== Request plumbing ====================

    /**
     * @brief Read a response body and surface documented API errors.
     *
     * v2 reports failures inside the envelope rather than through the status
     * code alone, so parsing happens even on success. Transport failures are
     * unwrapped here too: the body of an HttpStatusError usually holds the
     * actionable code (`app.errors.unauthorized`, `app.item.notFound`).
     */
    json parseResponse(const std::string &body, const std::string &context)
    {
        json root;
        try
        {
            root = json::parse(body);
        }
        catch (const json::parse_error &)
        {
            throw std::runtime_error(context + ": response was not valid JSON");
        }
        throwIfApiError(root, context);
        return root;
    }

    json request(const std::string &method, const std::string &url,
                 const json &payload, const HttpTransport::Headers &headers,
                 const std::string &context)
    {
        std::string body;
        HttpTransport::Headers response_headers;
        try
        {
            if (method == "GET")
            {
                body = http_client->Get(url, headers);
            }
            else if (method == "PATCH")
            {
                body = http_client->patch(url, payload, headers, response_headers);
            }
            else
            {
                body = http_client->post(url, payload, headers, response_headers);
            }
        }
        catch (const HttpStatusError &error)
        {
            // The status says "failed"; the body says why. Prefer the body.
            try
            {
                throwIfApiError(json::parse(error.body()), context);
            }
            catch (const ApiError &)
            {
                throw;
            }
            catch (const json::parse_error &)
            {
                // Fall through to the status-based message below.
            }
            throw std::runtime_error(context + ": HTTP " + std::to_string(error.status()));
        }

        return parseResponse(body, context);
    }

    // ==================== Order implementation ====================

    std::vector<Order> getMyOrdersImpl()
    {
        const std::string url = V2_API + "/orders/my";
        const json root = request("GET", url, json(), HeaderFactory::authHeaders(bearerToken()),
                                  "Failed to list orders");
        return toOrders(*wfm_json::requireDataArray(root, "Failed to list orders"));
    }

    /// Map order JSON to models, resolving item slugs through the item database.
    std::vector<Order> toOrders(const json &array) const
    {
        std::vector<Order> orders;
        orders.reserve(array.size());
        for (const auto &entry : array)
        {
            Order order = wfm_json::parseOrder(entry);
            if (!order.itemId.empty() && itemDB.hasId(order.itemId))
            {
                order.itemSlug = itemDB.getSlugFromId(order.itemId);
            }
            orders.push_back(order);
        }
        return orders;
    }

    std::string placeOrderImpl(const PlaceOrderRequest &request_data)
    {
        // Refuse locally when the item is unknown: without its metadata the
        // request cannot be checked, and guessing would send a bad body.
        if (!itemDB.hasSlug(request_data.itemSlug))
        {
            throw std::invalid_argument(
                "Unknown item slug: " + request_data.itemSlug +
                ". Call loadItems() before placing orders.");
        }

        const Item &item = itemDB.getItem(request_data.itemSlug);
        order_validation::validateAgainst(request_data, item);

        const json payload =
            order_validation::buildPayload(request_data, item.id);
        const std::string url = V2_API + "/order";
        const json root = request("POST", url, payload,
                                 HeaderFactory::authHeaders(bearerToken()),
                                 "Failed to create order");

        const json &data = *wfm_json::requireDataObject(root, "Failed to create order");
        if (!wfm_json::hasKey(data, "id"))
        {
            throw std::runtime_error("Failed to create order: response had no id");
        }
        return data.at("id").get<std::string>();
    }

    Order updateOrderImpl(const std::string &orderId, const UpdateOrderRequest &request_data)
    {
        if (!request_data.hasChanges())
        {
            throw std::invalid_argument("No fields provided to update");
        }

        const json payload = order_validation::buildPatchPayload(request_data);
        const std::string url = V2_API + "/order/" + orderId;
        const json root = request("PATCH", url, payload,
                                 HeaderFactory::authHeaders(bearerToken()),
                                 "Failed to update order");

        const json &data = *wfm_json::requireDataObject(root, "Failed to update order");
        return wfm_json::parseOrder(data);
    }

    Transaction closeOrderImpl(const std::string &orderId, int quantity)
    {
        if (quantity < 1)
        {
            throw std::invalid_argument("quantity: must be at least 1");
        }

        const json payload = {{"quantity", quantity}};
        const std::string url = V2_API + "/order/" + orderId + "/close";
        const json root = request("POST", url, payload,
                                 HeaderFactory::authHeaders(bearerToken()),
                                 "Failed to close order");

        const json &data = *wfm_json::requireDataObject(root, "Failed to close order");
        return wfm_json::parseTransaction(data);
    }

    /**
     * @brief Delete an order.
     *
     * The endpoint returns the deleted order, but HttpTransport::Delete
     * reports only the status, so the response body is not available here.
     * Callers that need the deleted order should capture it beforehand.
     */
    void deleteOrderImpl(const std::string &orderId)
    {
        const std::string url = V2_API + "/order/" + orderId;
        const long status = http_client->Delete(url, HeaderFactory::authHeaders(bearerToken()));
        if (status < 200 || status >= 300)
        {
            throw std::runtime_error("Failed to delete order: HTTP " + std::to_string(status));
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

    /**
     * @brief Set the marketplace platform and localization used by new requests.
     * @param newPlatform One of pc, ps4, xbox, switch, mobile.
     * @param newLanguage One of the documented language codes, e.g. "en".
     */
    void setPlatform(const std::string &newPlatform, const std::string &newLanguage = "en")
    {
        platform = newPlatform;
        language = newLanguage;
    }

    const std::string &getPlatform() const { return platform; }
    const std::string &getLanguage() const { return language; }

    /**
     * @brief Order operations, only reachable on an authenticated client.
     *
     * Obtained from `WarframeMarketAPI::authenticated()`, which refuses to
     * return one until a token is set.
     */
    class AuthenticatedClient
    {
    public:
        explicit AuthenticatedClient(WarframeMarketAPI &apiRef) : api(apiRef) {}

        std::vector<Order> getMyOrders()
        {
            api.ensureAuthenticated();
            return api.getMyOrdersImpl();
        }

        std::string placeOrder(const PlaceOrderRequest &request)
        {
            api.ensureAuthenticated();
            return api.placeOrderImpl(request);
        }

        /**
         * @brief Convenience overload for the simple, non-ranked case.
         *
         * Rejects items that need rank, charges, subtype, stars, or perTrade;
         * use the struct overload for those.
         */
        std::string placeOrder(const std::string &itemSlug, int platinum, int quantity,
                               bool visible)
        {
            PlaceOrderRequest request;
            request.itemSlug = itemSlug;
            request.platinum = platinum;
            request.quantity = quantity;
            request.visible = visible;
            return placeOrder(request);
        }

        Order updateOrder(const std::string &orderId, const UpdateOrderRequest &request)
        {
            api.ensureAuthenticated();
            return api.updateOrderImpl(orderId, request);
        }

        Transaction closeOrder(const std::string &orderId, int quantity)
        {
            api.ensureAuthenticated();
            return api.closeOrderImpl(orderId, quantity);
        }

        void deleteOrder(const std::string &orderId)
        {
            api.ensureAuthenticated();
            api.deleteOrderImpl(orderId);
        }

    private:
        WarframeMarketAPI &api;
    };

    // ==================== Authentication ====================

    /**
     * @brief Authenticate against the legacy v1 session endpoint.
     *
     * v2 `/auth/signin` is first-party only and requires Firebase App Check,
     * and public OAuth client registration is not open yet, so the v1
     * endpoint is the only path available to third-party clients. OAuth is
     * planned to replace it; this method is the seam that will change.
     *
     * @param email Account email.
     * @param password Account password.
     * @param device_id Optional device identifier bound to the session.
     * @param device_name Optional human-readable device name.
     * @return The access token and basic user info.
     */
    AuthResponse login(const std::string &email, const std::string &password,
                       const std::string &device_id = "",
                       const std::string &device_name = "wft-cpp")
    {
        json payload = {
            {"auth_type", "header"},
            {"email", email},
            {"password", password}};

        if (!device_id.empty())
        {
            payload["device_id"] = device_id;
        }
        if (!device_name.empty())
        {
            payload["device_name"] = device_name;
        }

        HttpTransport::Headers headers;
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "application/json";
        headers["User-Agent"] = HeaderFactory::userAgent();
        headers["Authorization"] = "JWT";

        const std::string url = V1_API + "/auth/signin";

        std::string body;
        HttpTransport::Headers response_headers;
        try
        {
            body = http_client->post(url, payload, headers, response_headers);
        }
        catch (const HttpStatusError &error)
        {
            throw std::runtime_error("Login failed: HTTP " +
                                     std::to_string(error.status()));
        }

        json root = json::parse(body);

        AuthResponse result;

        // The token is returned in the response header, whose name is
        // case-insensitive: the response header map is keyed that way so this
        // lookup cannot silently miss.
        auto token_it = response_headers.find("authorization");
        if (token_it != response_headers.end())
        {
            result.token = token_it->second;
        }

        if (wfm_json::hasKey(root, "payload") && root.at("payload").is_object() &&
            root.at("payload").contains("user") && root.at("payload").at("user").is_object())
        {
            const json &user = root.at("payload").at("user");
            if (wfm_json::hasKey(user, "id"))
            {
                result.user_id = user.at("id").get<std::string>();
            }
            if (wfm_json::hasKey(user, "ingame_name"))
            {
                result.ingame_name = user.at("ingame_name").get<std::string>();
            }
        }

        if (result.token.empty())
        {
            throw std::runtime_error("Login failed: no token in response");
        }

        return result;
    }

    /**
     * @brief Set the access token directly, for callers that already have one.
     *
     * Accepts the token with or without the legacy `JWT ` prefix.
     */
    void setJWT(const std::string &token)
    {
        accessToken = token;
        if (accessToken.empty())
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
        accessToken.clear();
        state.reset(new UnauthenticatedState());
    }

    std::string getJWT() const { return accessToken; }
    bool isAuthenticated() const { return state && state->isAuthenticated(); }

    /**
     * @brief Order-operation facade for an authenticated client.
     * @throws std::runtime_error when no token is set.
     */
    AuthenticatedClient authenticated()
    {
        ensureAuthenticated();
        return AuthenticatedClient(*this);
    }

    // ==================== Item Database ====================

    /**
     * @brief Load every tradable item, replacing anything already loaded.
     *
     * The full item model is retained because order validation needs each
     * item's rank, subtype, bulk-trading, and star metadata.
     *
     * @return Number of items loaded.
     * @throws std::runtime_error on an empty or malformed response.
     */
    size_t loadItems()
    {
        const std::string url = V2_API + "/items";
        const json root = request("GET", url, json(), contextHeaders(),
                                  "Failed to load items");
        const json &data = *wfm_json::requireDataArray(root, "Failed to load items");

        if (data.empty())
        {
            throw std::runtime_error("Failed to load items: API response contains no items");
        }

        ItemDatabase loaded;
        for (const auto &entry : data)
        {
            loaded.add(wfm_json::parseItem(entry));
        }

        // Swap in only after a full successful parse, so a mid-parse failure
        // leaves the existing index intact.
        itemDB = loaded;
        return itemDB.size();
    }

    /**
     * @brief Search loaded items by substring.
     * @param query Substring matched against slugs.
     */
    std::vector<std::string> searchItems(const std::string &query) const
    {
        return itemDB.searchByName(query);
    }

    /**
     * @brief Resolve an item id to its slug.
     * @throws std::runtime_error when the id is not loaded.
     */
    std::string getSlugByID(const std::string &id) const
    {
        return itemDB.getSlugFromId(id);
    }

    /**
     * @brief Resolve an item slug to its id.
     * @throws std::runtime_error when the slug is not loaded.
     */
    std::string getIDBySlug(const std::string &slug) const
    {
        return itemDB.getIdFromSlug(slug);
    }

    /**
     * @brief Full metadata for a loaded item.
     * @throws std::runtime_error when the slug is not loaded.
     */
    const Item &getItem(const std::string &slug) const
    {
        return itemDB.getItem(slug);
    }

    const std::vector<std::string> &getAllSlugs() const
    {
        return itemDB.allSlugs;
    }

    size_t getItemCount() const
    {
        return itemDB.size();
    }
};

#endif // WFM_API_H
