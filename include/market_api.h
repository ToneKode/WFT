#ifndef MARKET_API_H
#define MARKET_API_H

#include <cstddef>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "api_error.h"
#include "constants.h"
#include "http_client.h"
#include "http_transport.h"
#include "order_book.h"
#include "types.h"
#include "wfm_json.h"

// ==================== Request Options ====================
/**
 * @brief Optional rank filters for `MarketAPI::topOrdersForItem()`.
 *
 * `rankLt` wins over `rank`, as the API does: a caller that sets both gets the
 * "lower than" filter rather than a silently ignored field. An absent value
 * means "no filter", which is why this is `std::optional` and not `-1`.
 */
struct RankFilter
{
    std::optional<int> rank = std::nullopt;
    std::optional<int> rankLt = std::nullopt;
};

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
    MarketAPI() : http_client_(std::make_unique<HttpClient>()) {}

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
    void setContext(std::string_view newPlatform, std::string_view newLanguage = "en",
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
        constexpr std::string_view context = "Failed to list items";
        const json root = get(std::format("{}/items", V2_API), context);
        const json &data = wfm_json::requireDataArray(root, context);

        std::vector<Item> items;
        items.reserve(data.size());
        std::ranges::transform(data, std::back_inserter(items), wfm_json::parseItem);
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
        std::string itemsHash = {}; ///< `data.collections.items`, base64.
        std::string updatedAt = {}; ///< `data.updatedAt`, ISO 8601.
    };

    /**
     * @brief Current collection versions.
     *
     * One small request replaces `items()` when nothing has changed. Pair it
     * with `item_cache::saveItems()`/`loadItems()`.
     */
    Versions versions()
    {
        constexpr std::string_view context = "Failed to read versions";
        const json root = get(std::format("{}/versions", V2_API), context);
        const json &data = wfm_json::requireDataObject(root, context);

        Versions result;
        // A wrong-typed field must not throw: an empty hash just means
        // "refresh", which is the safe outcome.
        if (const auto collections = data.find("collections");
            collections != data.end() && collections->is_object())
        {
            if (const auto items = collections->find("items");
                items != collections->end() && items->is_string())
            {
                result.itemsHash = items->get<std::string>();
            }
        }
        if (const auto updatedAt = data.find("updatedAt");
            updatedAt != data.end() && updatedAt->is_string())
        {
            result.updatedAt = updatedAt->get<std::string>();
        }
        return result;
    }

    /**
     * @brief One item by slug.
     * @throws ApiError carrying `app.item.notFound` for an unknown slug.
     */
    Item item(std::string_view slug)
    {
        constexpr std::string_view context = "Failed to get item";
        const json root = get(std::format("{}/item/{}", V2_API, slug), context);
        const json &data = wfm_json::requireDataObject(root, context);
        return wfm_json::parseItem(data);
    }

    /**
     * @brief Visible orders for one item.
     *
     * Do not rank the result by `platinum`: that field is a bundle total. Use
     * `bookForItem()` or `Order::platinumPerUnit()`.
     */
    std::vector<Order> ordersForItem(std::string_view slug)
    {
        constexpr std::string_view context = "Failed to list item orders";
        const json root = get(std::format("{}/orders/item/{}", V2_API, slug), context);
        const json &data = wfm_json::requireDataArray(root, context);
        return parseOrders(data);
    }

    /// Fetch an item's orders and index them, in one call.
    OrderBook bookForItem(std::string_view slug)
    {
        OrderBook book;
        book.load(ordersForItem(slug));
        return book;
    }

    /// Up to 5 sell and 5 buy orders as ranked by the server.
    struct TopOrders
    {
        std::vector<Order> sell = {};
        std::vector<Order> buy = {};
    };

    /**
     * @brief Cheapest sell orders for one item, for `bestSells()` results.
     *
     * `sells` is normalised per unit already: `Order::platinumPerUnit()` is the
     * comparable number, not `Order::platinum`.
     */
    struct ItemListings
    {
        std::string itemSlug = {};
        std::vector<Order> sells = {};
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
    std::vector<ItemListings> bestSells(const std::vector<std::string> &slugs,
                                        std::size_t limit = 5)
    {
        std::vector<ItemListings> listings;
        listings.reserve(slugs.size());

        for (const auto &slug : slugs)
        {
            const OrderBook book = bookForItem(slug);
            const std::vector<Order> &sells = book.sells();
            const std::size_t keep = (limit == 0) ? sells.size() : (std::min)(limit, sells.size());

            ItemListings entry;
            entry.itemSlug = slug;
            std::ranges::copy_n(sells.begin(), keep, std::back_inserter(entry.sells));
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
     * @param slug Item slug.
     * @param filter Optional rank filters; `rankLt` takes precedence over
     *        `rank`, as the API does.
     */
    TopOrders topOrdersForItem(std::string_view slug, const RankFilter &filter = {})
    {
        constexpr std::string_view context = "Failed to list top orders";

        std::string url = std::format("{}/orders/item/{}/top", V2_API, slug);
        if (filter.rankLt)
        {
            url += std::format("?rankLt={}", *filter.rankLt);
        }
        else if (filter.rank)
        {
            url += std::format("?rank={}", *filter.rank);
        }

        const json root = get(url, context);
        const json &data = wfm_json::requireDataObject(root, context);

        TopOrders top;
        if (const auto sell = data.find("sell"); sell != data.end() && sell->is_array())
        {
            top.sell = parseOrders(*sell);
        }
        if (const auto buy = data.find("buy"); buy != data.end() && buy->is_array())
        {
            top.buy = parseOrders(*buy);
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
        constexpr std::string_view context = "Failed to list recent orders";
        const json root = get(std::format("{}/orders/recent", V2_API), context);
        return parseOrders(wfm_json::requireDataArray(root, context));
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
    json get(std::string_view url, std::string_view context)
    {
        std::string body;
        try
        {
            body = http_client_->Get(std::string(url), contextHeaders());
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
            throw std::runtime_error(std::format("{}: response was not valid JSON", context));
        }
        throwIfApiError(root, context);
        return root;
    }

    /// Unwrap the API error code from a failed response body.
    static void throwFromErrorBody(const HttpStatusError &error, std::string_view context)
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
        throw std::runtime_error(std::format("{}: HTTP {}", context, error.status()));
    }

    static std::vector<Order> parseOrders(const json &array)
    {
        std::vector<Order> orders;
        orders.reserve(array.size());
        std::ranges::transform(array, std::back_inserter(orders), wfm_json::parseOrder);
        return orders;
    }
};

#endif // MARKET_API_H
