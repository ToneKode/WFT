#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

// ==================== Market Data ====================
/**
 * @brief Tradable item from the public items endpoints.
 *
 * Optional metadata uses `hasX` flags: this library is C++14, and a missing
 * field is not the same as zero.
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

    /// True when the item has a max rank.
    bool requiresRank() const { return hasMaxRank; }
    /// True when the item has subtypes.
    bool requiresSubtype() const { return !subtypes.empty(); }
    /// True when the item is bulk-tradable.
    bool requiresPerTrade() const { return hasBulkTradable && bulkTradable; }
    /// True when the item has amber or cyan star slots.
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
 * @brief Order from a public orders endpoint or the new-order feed.
 *
 * `user` is set when the payload embeds the owner. A missing user is unknown,
 * not offline.
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

#endif // TYPES_H
