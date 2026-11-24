#ifndef WFM_JSON_H
#define WFM_JSON_H

#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include "types.h"

/**
 * @file wfm_json.h
 * @brief Mapping between v2 JSON payloads and the public model types.
 *
 * Parsing lives here rather than inside MarketAPI so response shape handling
 * stays in one place.
 *
 * Optional fields are detected with `hasKey()` rather than defaulted, because
 * "absent" and "zero" carry different meaning for order fields.
 */

namespace wfm_json
{

inline bool hasKey(const nlohmann::json &object, const char *key)
{
    return object.is_object() && object.find(key) != object.end() && !object.at(key).is_null();
}

/**
 * @brief Parse one item, including optional trading metadata.
 * @throws std::runtime_error when the required `id` or `slug` is missing.
 */
inline Item parseItem(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        throw std::runtime_error("Invalid item: expected an object");
    }
    if (!hasKey(json, "id") || !hasKey(json, "slug"))
    {
        throw std::runtime_error("Invalid item: missing id or slug");
    }

    Item item;
    item.id = json.at("id").get<std::string>();
    item.slug = json.at("slug").get<std::string>();

    if (hasKey(json, "gameRef"))
    {
        item.gameRef = json.at("gameRef").get<std::string>();
    }
    if (hasKey(json, "tradable"))
    {
        item.tradable = json.at("tradable").get<bool>();
    }
    if (hasKey(json, "bulkTradable"))
    {
        item.hasBulkTradable = true;
        item.bulkTradable = json.at("bulkTradable").get<bool>();
    }
    if (hasKey(json, "maxRank"))
    {
        item.hasMaxRank = true;
        item.maxRank = json.at("maxRank").get<int>();
    }
    if (hasKey(json, "maxCharges"))
    {
        item.hasMaxCharges = true;
        item.maxCharges = json.at("maxCharges").get<int>();
    }
    if (hasKey(json, "maxAmberStars"))
    {
        item.hasMaxAmberStars = true;
        item.maxAmberStars = json.at("maxAmberStars").get<int>();
    }
    if (hasKey(json, "maxCyanStars"))
    {
        item.hasMaxCyanStars = true;
        item.maxCyanStars = json.at("maxCyanStars").get<int>();
    }
    if (hasKey(json, "setRoot"))
    {
        item.hasSetRoot = true;
        item.setRoot = json.at("setRoot").get<bool>();
    }
    if (hasKey(json, "tradingTax"))
    {
        item.hasTradingTax = true;
        item.tradingTax = json.at("tradingTax").get<int>();
    }
    if (hasKey(json, "subtypes") && json.at("subtypes").is_array())
    {
        for (const auto &subtype : json.at("subtypes"))
        {
            if (subtype.is_string())
            {
                item.subtypes.push_back(subtype.get<std::string>());
            }
        }
    }

    // The display name is nested per language; prefer the requested language,
    // then English, then whatever the server sent first.
    if (hasKey(json, "i18n") && json.at("i18n").is_object())
    {
        const nlohmann::json &i18n = json.at("i18n");
        if (i18n.contains("en") && i18n.at("en").is_object() &&
            hasKey(i18n.at("en"), "name"))
        {
            item.itemName = i18n.at("en").at("name").get<std::string>();
        }
        else if (!i18n.empty() && i18n.begin().value().is_object() &&
                 hasKey(i18n.begin().value(), "name"))
        {
            item.itemName = i18n.begin().value().at("name").get<std::string>();
        }
    }

    return item;
}

/**
 * @brief Serialise an item in the shape `parseItem` reads back.
 *
 * Used by the item cache: writing the API's own field names means the load path
 * reuses `parseItem` instead of a second mapping that could drift from it.
 * Optional metadata is written only when its `hasX` flag is set, because
 * "absent" and "zero" are not the same value.
 */
inline nlohmann::json toItemJson(const Item &item)
{
    nlohmann::json out = {{"id", item.id}, {"slug", item.slug}, {"tradable", item.tradable}};
    if (!item.itemName.empty())
    {
        out["i18n"] = {{"en", {{"name", item.itemName}}}};
    }
    if (!item.gameRef.empty())
    {
        out["gameRef"] = item.gameRef;
    }
    if (item.hasBulkTradable)
    {
        out["bulkTradable"] = item.bulkTradable;
    }
    if (item.hasMaxRank)
    {
        out["maxRank"] = item.maxRank;
    }
    if (item.hasMaxCharges)
    {
        out["maxCharges"] = item.maxCharges;
    }
    if (item.hasMaxAmberStars)
    {
        out["maxAmberStars"] = item.maxAmberStars;
    }
    if (item.hasMaxCyanStars)
    {
        out["maxCyanStars"] = item.maxCyanStars;
    }
    if (!item.subtypes.empty())
    {
        out["subtypes"] = item.subtypes;
    }
    if (item.hasSetRoot)
    {
        out["setRoot"] = item.setRoot;
    }
    if (item.hasTradingTax)
    {
        out["tradingTax"] = item.tradingTax;
    }
    return out;
}

/**
 * @brief Parse the shortened user object embedded in orders.
 * @throws std::runtime_error when `id` is missing.
 */
inline UserShort parseUserShort(const nlohmann::json &json)
{
    if (!json.is_object() || !hasKey(json, "id"))
    {
        throw std::runtime_error("Invalid user: missing id");
    }

    UserShort user;
    user.id = json.at("id").get<std::string>();

    if (hasKey(json, "ingameName"))
    {
        user.ingameName = json.at("ingameName").get<std::string>();
    }
    if (hasKey(json, "slug"))
    {
        user.slug = json.at("slug").get<std::string>();
    }
    if (hasKey(json, "platform"))
    {
        user.platform = json.at("platform").get<std::string>();
    }
    if (hasKey(json, "locale"))
    {
        user.locale = json.at("locale").get<std::string>();
    }
    if (hasKey(json, "status"))
    {
        user.status = json.at("status").get<std::string>();
    }
    if (hasKey(json, "lastSeen"))
    {
        user.lastSeen = json.at("lastSeen").get<std::string>();
    }
    if (hasKey(json, "reputation"))
    {
        user.reputation = json.at("reputation").get<long>();
    }
    if (hasKey(json, "crossplay"))
    {
        user.crossplay = json.at("crossplay").get<bool>();
    }

    return user;
}

/**
 * @brief Parse one order.
 * @throws std::runtime_error when a required field is missing or mistyped.
 *
 * `itemSlug` stays empty: public order payloads carry `itemId`, not a slug.
 */
inline Order parseOrder(const nlohmann::json &json)
{
    if (!json.is_object())
    {
        throw std::runtime_error("Invalid order: expected an object");
    }
    if (!hasKey(json, "id"))
    {
        throw std::runtime_error("Invalid order: missing id");
    }

    Order order;
    order.id = json.at("id").get<std::string>();

    if (hasKey(json, "type"))
    {
        order.orderType = json.at("type").get<std::string>();
    }
    if (hasKey(json, "platinum"))
    {
        order.platinum = json.at("platinum").get<int>();
    }
    if (hasKey(json, "quantity"))
    {
        order.quantity = json.at("quantity").get<int>();
    }
    if (hasKey(json, "visible"))
    {
        order.visible = json.at("visible").get<bool>();
    }
    if (hasKey(json, "itemId"))
    {
        order.itemId = json.at("itemId").get<std::string>();
    }
    if (hasKey(json, "createdAt"))
    {
        order.createdAt = json.at("createdAt").get<std::string>();
    }
    if (hasKey(json, "updatedAt"))
    {
        order.updatedAt = json.at("updatedAt").get<std::string>();
    }

    if (hasKey(json, "perTrade"))
    {
        order.hasPerTrade = true;
        order.perTrade = json.at("perTrade").get<int>();
    }
    if (hasKey(json, "subtype"))
    {
        order.hasSubtype = true;
        order.subtype = json.at("subtype").get<std::string>();
    }
    if (hasKey(json, "rank"))
    {
        order.hasRank = true;
        order.rank = json.at("rank").get<int>();
    }
    if (hasKey(json, "charges"))
    {
        order.hasCharges = true;
        order.charges = json.at("charges").get<int>();
    }
    if (hasKey(json, "amberStars"))
    {
        order.hasAmberStars = true;
        order.amberStars = json.at("amberStars").get<int>();
    }
    if (hasKey(json, "cyanStars"))
    {
        order.hasCyanStars = true;
        order.cyanStars = json.at("cyanStars").get<int>();
    }
    if (hasKey(json, "user") && json.at("user").is_object())
    {
        order.hasUser = true;
        order.user = parseUserShort(json.at("user"));
    }

    return order;
}

/**
 * @brief Extract the `data` array from a v2 envelope.
 * @param root Parsed response envelope.
 * @param context Prefix for the error message, e.g. the operation name.
 * @return The array; reference is owned by `root`.
 * @throws std::runtime_error when `data` is missing or not an array.
 *
 * Returns a pointer rather than a reference because GCC's -Wdangling-reference
 * cannot tell that the result outlives this call when the context argument is
 * a temporary string, and treating that warning as an error is the point of
 * the CI configuration.
 */
inline const nlohmann::json *requireDataArray(const nlohmann::json &root,
                                              const char *context)
{
    if (!root.is_object() || !root.contains("data") || !root.at("data").is_array())
    {
        throw std::runtime_error(std::string(context) + ": invalid API response format");
    }
    return &root.at("data");
}

/**
 * @brief Extract the `data` object from a v2 envelope.
 * @return The object; reference is owned by `root`.
 * @throws std::runtime_error when `data` is missing or not an object.
 */
inline const nlohmann::json *requireDataObject(const nlohmann::json &root,
                                               const char *context)
{
    if (!root.is_object() || !root.contains("data") || !root.at("data").is_object())
    {
        throw std::runtime_error(std::string(context) + ": invalid API response format");
    }
    return &root.at("data");
}

} // namespace wfm_json

#endif // WFM_JSON_H
