#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

// ==================== Authentication ====================
struct AuthResponse
{
    std::string token;
    std::string user_id;
    std::string ingame_name;
};

// ==================== Market Data ====================
/**
 * @brief Tradable item plus the metadata that order validation needs.
 *
 * The v2 API is strict about which order fields are allowed: `perTrade` is
 * required for bulk-tradable items and forbidden otherwise, `rank` likewise
 * for ranked items, `subtype` for items with subtypes, and so on. Those rules
 * are only checkable if the item's own metadata is kept, so the lookup
 * database stores the fields below rather than id/slug alone.
 *
 * Optional metadata is represented by the `hasX` flags for the same reason the
 * request structs do it: the library is C++14 and cannot use std::optional.
 */
struct Item
{
    std::string id;
    std::string slug;

    // i18n.en.name, when the request asked for localization.
    std::string itemName;
    std::string gameRef;

    bool tradable = true;

    bool hasBulkTradable = false;
    bool bulkTradable = false;

    bool hasMaxRank = false;
    int maxRank = 0;

    bool hasMaxCharges = false;
    int maxCharges = 0;

    bool hasMaxAmberStars = false;
    int maxAmberStars = 0;

    bool hasMaxCyanStars = false;
    int maxCyanStars = 0;

    std::vector<std::string> subtypes;

    bool hasSetRoot = false;
    bool setRoot = false;

    bool hasTradingTax = false;
    int tradingTax = 0;

    /// True when creating an order for this item requires `rank`.
    bool requiresRank() const { return hasMaxRank; }
    /// True when creating an order for this item requires `subtype`.
    bool requiresSubtype() const { return !subtypes.empty(); }
    /// True when creating an order for this item requires `perTrade`.
    bool requiresPerTrade() const { return hasBulkTradable && bulkTradable; }
    /// True when creating an order for this item requires star counts.
    bool requiresStars() const { return hasMaxAmberStars || hasMaxCyanStars; }
};

// ==================== User Models ====================
/**
 * @brief Shortened user model embedded in orders and transactions.
 *
 * `status` is the field that decides whether a counterparty can actually trade
 * right now. The API reports `invisible` as `offline`.
 */
struct UserShort
{
    std::string id;
    std::string ingameName;
    std::string slug;
    std::string platform;
    std::string locale;
    std::string status; // "offline", "online", "ingame"
    std::string lastSeen;
    long reputation = 0;
    bool crossplay = false;

    /// True when the user is currently reachable for a trade.
    bool isOnline() const { return status == "online" || status == "ingame"; }
};

// ==================== Order Models ====================
/**
 * @brief Order as returned by the v2 API.
 *
 * `user` is only populated by endpoints that embed the owner (item orders,
 * recent orders, user orders); `/orders/my` omits it because the owner is the
 * caller.
 */
struct Order
{
    std::string id;
    std::string orderType; // "buy" or "sell"
    int platinum = 0;
    int quantity = 0;
    bool visible = false;
    std::string itemId;
    std::string itemSlug;
    std::string createdAt;
    std::string updatedAt;

    bool hasPerTrade = false;
    int perTrade = 0;

    bool hasSubtype = false;
    std::string subtype;

    bool hasRank = false;
    int rank = 0;

    bool hasCharges = false;
    int charges = 0;

    bool hasAmberStars = false;
    int amberStars = 0;

    bool hasCyanStars = false;
    int cyanStars = 0;

    bool hasUser = false;
    UserShort user;

    /// Platinum per single unit, using `perTrade` when the order is bundled.
    double platinumPerUnit() const
    {
        const int units = (hasPerTrade && perTrade > 0) ? perTrade : 1;
        return static_cast<double>(platinum) / static_cast<double>(units);
    }
};

/**
 * @brief Result of closing an order.
 *
 * Close returns a Transaction, not an Order. A full close deletes the order and
 * a partial close leaves it with a reduced quantity, so callers need the
 * remaining quantity rather than the closed one.
 */
struct Transaction
{
    std::string id;
    std::string type;
    std::string originId;
    double platinum = 0.0;
    int quantity = 0;
    std::string createdAt;
    std::string updatedAt;
};

// ==================== Order Requests ====================
/**
 * @brief Partial update for PATCH /v2/order/{id}.
 *
 * Each field is gated by a `hasX` flag because omitting a field and sending a
 * zero value mean different things to the API.
 */
struct UpdateOrderRequest
{
    bool hasPlatinum = false;
    int platinum = 0;

    bool hasQuantity = false;
    int quantity = 0;

    bool hasVisible = false;
    bool visible = true;

    bool hasRank = false;
    int rank = 0;

    bool hasPerTrade = false;
    int perTrade = 0;

    bool hasCharges = false;
    int charges = 0;

    bool hasSubtype = false;
    std::string subtype;

    bool hasAmberStars = false;
    int amberStars = 0;

    bool hasCyanStars = false;
    int cyanStars = 0;

    bool hasChanges() const
    {
        return hasPlatinum || hasQuantity || hasVisible || hasRank || hasPerTrade ||
               hasCharges || hasSubtype || hasAmberStars || hasCyanStars;
    }
};

/// Order type accepted by the create and update endpoints.
enum class OrderType
{
    Sell,
    Buy
};

/**
 * @brief Request for POST /v2/order.
 *
 * The API rejects fields that do not apply to the item, so callers must set
 * exactly the optional fields that apply to the item being traded. Use
 * `validateAgainst()` to check that before spending a request.
 */
struct PlaceOrderRequest
{
    std::string itemSlug;
    OrderType type = OrderType::Sell;
    int platinum = 0;
    int quantity = 1;
    bool visible = true;

    bool hasPerTrade = false;
    int perTrade = 0;

    bool hasRank = false;
    int rank = 0;

    bool hasCharges = false;
    int charges = 0;

    bool hasSubtype = false;
    std::string subtype;

    bool hasAmberStars = false;
    int amberStars = 0;

    bool hasCyanStars = false;
    int cyanStars = 0;
};

/// Previous name for PlaceOrderRequest, kept so existing callers compile.
using SellOrderRequest = PlaceOrderRequest;

#endif // TYPES_H
