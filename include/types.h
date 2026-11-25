#ifndef TYPES_H
#define TYPES_H

#include <optional>
#include <string>
#include <vector>

// ==================== Market Data ====================
/**
 * @brief Tradable item from the public items endpoints.
 *
 * Field presence matters: the API omits metadata an item does not have, and a
 * missing `maxRank` is not the same as a rank of zero. Absent fields are
 * `std::nullopt`, so `maxRank.value_or(0)` is only correct when zero is a
 * meaningful default for that field.
 */
struct Item
{
    std::string id;
    std::string slug;

    // i18n.en.name, when the request asked for localization.
    std::string itemName = {};
    std::string gameRef = {};

    bool tradable = true;

    std::optional<bool> bulkTradable = std::nullopt;
    std::optional<int> maxRank = std::nullopt;
    std::optional<int> maxCharges = std::nullopt;
    std::optional<int> maxAmberStars = std::nullopt;
    std::optional<int> maxCyanStars = std::nullopt;

    std::vector<std::string> subtypes = {};

    std::optional<bool> setRoot = std::nullopt;
    std::optional<int> tradingTax = std::nullopt;

    /// True when the item has a max rank.
    [[nodiscard]] bool requiresRank() const { return maxRank.has_value(); }
    /// True when the item has subtypes.
    [[nodiscard]] bool requiresSubtype() const { return !subtypes.empty(); }
    /// True when the item is bulk-tradable.
    [[nodiscard]] bool requiresPerTrade() const { return bulkTradable.value_or(false); }
    /// True when the item has amber or cyan star slots.
    [[nodiscard]] bool requiresStars() const
    {
        return maxAmberStars.has_value() || maxCyanStars.has_value();
    }
};

// ==================== User Models ====================
/**
 * @brief Shortened user model embedded in orders.
 *
 * `status` is the field that decides whether a counterparty can actually trade
 * right now. The API reports `invisible` as `offline`.
 */
struct UserShort
{
    std::string id;
    std::string ingameName = {};
    std::string slug = {};
    std::string platform = {};
    std::string locale = {};
    std::string status = {}; // "offline", "online", "ingame"
    std::string lastSeen = {};
    long reputation = 0;
    bool crossplay = false;

    /// True when the user is currently reachable for a trade.
    [[nodiscard]] bool isOnline() const
    {
        return status == "online" || status == "ingame";
    }
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
    std::string orderType = {}; // "buy" or "sell"
    int platinum = 0;
    int quantity = 0;
    bool visible = false;
    std::string itemId = {};
    std::string itemSlug = {};
    std::string createdAt = {};
    std::string updatedAt = {};

    std::optional<int> perTrade = std::nullopt;
    std::optional<std::string> subtype = std::nullopt;
    std::optional<int> rank = std::nullopt;
    std::optional<int> charges = std::nullopt;
    std::optional<int> amberStars = std::nullopt;
    std::optional<int> cyanStars = std::nullopt;

    std::optional<UserShort> user = std::nullopt;

    /// Platinum per single unit, using `perTrade` when the order is bundled.
    [[nodiscard]] constexpr double platinumPerUnit() const
    {
        const int units = (perTrade && *perTrade > 0) ? *perTrade : 1;
        return static_cast<double>(platinum) / static_cast<double>(units);
    }
};

#endif // TYPES_H
