#ifndef MARKET_API_H
#define MARKET_API_H

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "api_error.h"
#include "constants.h"
#include "http_client.h"
#include "http_transport.h"
#include "order_book.h"
#include "types.h"
#include "wfm_json.h"

/**
 * @file market_api.h
 * @brief Anonymous read-only marketplace data.
 *
 * No token and no account. Callers own caching. The API rules ask for it, and
 * `/v2/versions` exists to tell you when a cached item list has gone stale.
 */
class MarketAPI
{
public:
    MarketAPI() : http_client_(new HttpClient()) {}

    /**
     * @brief Construct with an alternate transport (tests, proxies, stubs).
     * @param transport Non-null transport; ownership is transferred.
     */
    explicit MarketAPI(std::unique_ptr<HttpTransport> transport)
        : http_client_(std::move(transport))
    {
        if (!http_client_)
        {
            throw std::invalid_argument("MarketAPI requires a transport");
        }
    }

    /**
     * @brief Set the platform, language, and crossplay scope for later calls.
     * @param newPlatform One of pc, ps4, xbox, switch, mobile.
     */
    void setContext(const std::string &newPlatform, const std::string &newLanguage = "en",
                    bool crossplay = true)
    {
        platform_ = newPlatform;
        language_ = newLanguage;
        crossplay_ = crossplay;
    }

    /**
     * @brief Every tradable item.
     */
    std::vector<Item> items()
    {
        const json root = get(V2_API + "/items", "Failed to list items");
        const json &data = *wfm_json::requireDataArray(root, "Failed to list items");

        std::vector<Item> items;
        items.reserve(data.size());
        for (const auto &entry : data)
        {
            items.push_back(wfm_json::parseItem(entry));
        }
        return items;
    }

    /**
     * @brief The subset of `/v2/versions` that tells a cache when to refresh.
     *
     * `itemsHash` is opaque and only useful for equality: compare the hash you
     * saved alongside a cached item list with the hash reported here, and
     * re-fetch only when they differ.
     */
    struct Versions
    {
        std::string itemsHash; ///< `data.collections.items`, base64.
        std::string updatedAt; ///< `data.updatedAt`, ISO 8601.
    };

    /**
     * @brief Current collection versions.
     *
     * One small request replaces `items()` when nothing has changed. Pair it
     * with `item_cache::saveItems()`/`loadItems()`.
     */
    Versions versions()
    {
        const json root = get(V2_API + "/versions", "Failed to read versions");
        const json &data = *wfm_json::requireDataObject(root, "Failed to read versions");

        Versions result;
        if (data.contains("collections") && data.at("collections").is_object())
        {
            const json &collections = data.at("collections");
            if (collections.contains("items") && collections.at("items").is_string())
            {
                result.itemsHash = collections.at("items").get<std::string>();
            }
        }
        if (data.contains("updatedAt") && data.at("updatedAt").is_string())
        {
            result.updatedAt = data.at("updatedAt").get<std::string>();
        }
        return result;
    }

    /**
     * @brief One item by slug.
     * @throws ApiError carrying `app.item.notFound` for an unknown slug.
     */
    Item item(const std::string &slug)
    {
        const json root = get(V2_API + "/item/" + slug, "Failed to get item");
        return wfm_json::parseItem(*wfm_json::requireDataObject(root, "Failed to get item"));
    }

    /**
     * @brief Visible orders for one item.
     *
     * Do not rank the result by `platinum`: that field is a bundle total. Use
     * `bookForItem()` or `Order::platinumPerUnit()`.
     */
    std::vector<Order> ordersForItem(const std::string &slug)
    {
        const json root = get(V2_API + "/orders/item/" + slug, "Failed to list item orders");
        return parseOrders(*wfm_json::requireDataArray(root, "Failed to list item orders"));
    }

    /// Fetch an item's orders and index them, in one call.
    OrderBook bookForItem(const std::string &slug)
    {
        OrderBook book;
        book.load(ordersForItem(slug));
        return book;
    }

    /// Up to 5 sell and 5 buy orders as ranked by the server.
    struct TopOrders
    {
        std::vector<Order> sell;
        std::vector<Order> buy;
    };

    /**
     * @brief Cheapest sell orders for one item, for `bestSells()` results.
     *
     * `sells` is normalised per unit already: `Order::platinumPerUnit()` is the
     * comparable number, not `Order::platinum`.
     */
    struct ItemListings
    {
        std::string itemSlug;
        std::vector<Order> sells;
    };

    /**
     * @brief Best sell listings across several items, cheapest per unit first.
     *
     * One request per slug, paced by the transport's shared rate limiter, so a
     * wide batch is slow rather than throttled. Duplicate slugs are fetched
     * twice; pass a unique list.
     *
     * @param slugs Item slugs to price.
     * @param limit Maximum listings to keep per item; 0 keeps all of them.
     * @return One entry per slug, in the order given. An item with no sell
     *         orders yields an entry with an empty `sells`.
     * @throws ApiError when a request fails; the batch stops rather than
     *         returning partial results that look complete.
     *
     * Prefer `topOrdersForItem()` for Endo and Ayatan star items, whose ranking
     * has server-side rules `OrderBook` does not replicate.
     */
    std::vector<ItemListings> bestSells(const std::vector<std::string> &slugs, size_t limit = 5)
    {
        std::vector<ItemListings> listings;
        listings.reserve(slugs.size());
        for (const auto &slug : slugs)
        {
            const OrderBook book = bookForItem(slug);

            ItemListings entry;
            entry.itemSlug = slug;
            const std::vector<Order> &sells = book.sells();
            const size_t keep = (limit == 0) ? sells.size() : (std::min)(limit, sells.size());
            entry.sells.assign(sells.begin(), sells.begin() + static_cast<std::ptrdiff_t>(keep));
            listings.push_back(std::move(entry));
        }
        return listings;
    }

    /**
     * @brief The server's own top orders for an item.
     *
     * Worth preferring over a local sort for Endo and star items, whose
     * ranking has extra rules this library does not replicate.
     *
     * @param rank Optional exact rank filter.
     * @param rankLt Optional lower-than-rank filter; takes precedence over
     *        `rank`, as the API does.
     */
    TopOrders topOrdersForItem(const std::string &slug, int rank = -1, int rankLt = -1)
    {
        std::string url = V2_API + "/orders/item/" + slug + "/top";
        if (rankLt >= 0)
        {
            url += "?rankLt=" + std::to_string(rankLt);
        }
        else if (rank >= 0)
        {
            url += "?rank=" + std::to_string(rank);
        }

        const json root = get(url, "Failed to list top orders");
        const json &data = *wfm_json::requireDataObject(root, "Failed to list top orders");

        TopOrders top;
        if (data.contains("sell") && data.at("sell").is_array())
        {
            top.sell = parseOrders(data.at("sell"));
        }
        if (data.contains("buy") && data.at("buy").is_array())
        {
            top.buy = parseOrders(data.at("buy"));
        }
        return top;
    }

    /**
     * @brief Recent marketplace-wide orders.
     *
     * Up to 500 created in the last 4 hours by online users, newest first,
     * refreshed once a minute server-side. Cheaper than polling every item.
     */
    std::vector<Order> recentOrders()
    {
        const json root = get(V2_API + "/orders/recent", "Failed to list recent orders");
        return parseOrders(*wfm_json::requireDataArray(root, "Failed to list recent orders"));
    }

private:
    std::unique_ptr<HttpTransport> http_client_;
    std::string platform_ = "pc";
    std::string language_ = "en";
    bool crossplay_ = true;

    HttpTransport::Headers contextHeaders() const
    {
        HttpTransport::Headers headers;
        headers["Accept"] = "application/json";
        headers["User-Agent"] = WFT_USER_AGENT;
        headers["Platform"] = platform_;
        headers["Language"] = language_;
        headers["Crossplay"] = crossplay_ ? "true" : "false";
        return headers;
    }

    /**
     * @brief GET a URL and parse the envelope.
     *
     * A 4xx carries the useful code in its body, so the body is parsed before
     * the status is reported: "app.item.notFound" beats "HTTP 404".
     */
    json get(const std::string &url, const std::string &context)
    {
        std::string body;
        try
        {
            body = http_client_->Get(url, contextHeaders());
        }
        catch (const HttpStatusError &error)
        {
            throwFromErrorBody(error, context);
        }

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

    /// Unwrap the API error code from a failed response body.
    void throwFromErrorBody(const HttpStatusError &error, const std::string &context)
    {
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
        }
        throw std::runtime_error(context + ": HTTP " + std::to_string(error.status()));
    }

    std::vector<Order> parseOrders(const json &array) const
    {
        std::vector<Order> orders;
        orders.reserve(array.size());
        for (const auto &entry : array)
        {
            orders.push_back(wfm_json::parseOrder(entry));
        }
        return orders;
    }
};

#endif // MARKET_API_H
