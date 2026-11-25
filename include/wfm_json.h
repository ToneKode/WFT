#ifndef WFM_JSON_H
#define WFM_JSON_H

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "types.h"

/**
 * @file wfm_json.h
 * @brief Mapping between v2 JSON payloads and the public model types.
 *
 * Parsing lives here rather than inside MarketAPI so response shape handling
 * stays in one place.
 *
 * Optional fields are read through `optionalValue()` rather than defaulted,
 * because "absent" and "zero" carry different meaning for order fields.
 */

namespace wfm_json
{

/// True when `key` is present and not null.
inline bool hasKey(const nlohmann::json &object, std::string_view key)
{
    return object.is_object() && object.find(key) != object.end() && !object.at(key).is_null();
}

/**
 * @brief Read an optional field, or `std::nullopt` when it is absent or null.
 *
 * A present field of the wrong type still throws, as the required fields do:
 * silently dropping a mistyped value would hide a server-side shape change.
 */
template <typename T>
std::optional<T> optionalValue(const nlohmann::json &object, std::string_view key)
{
    if (!hasKey(object, key))
    {
        return std::nullopt;
    }
    return object.at(key).get<T>();
}

/**
 * @brief Write an optional field, omitting it when it holds no value.
 *
 * `std::optional` already distinguishes absent from zero, so the cache can
 * round-trip "the server did not send this" without a parallel flag.
 */
template <typename T>
void writeOptional(nlohmann::json &out, std::string_view key, const std::optional<T> &value)
{
    if (value)
    {
        out[std::string(key)] = *value;
    }
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

    item.gameRef = optionalValue<std::string>(json, "gameRef").value_or("");
    item.tradable = optionalValue<bool>(json, "tradable").value_or(true);
    item.bulkTradable = optionalValue<bool>(json, "bulkTradable");
    item.maxRank = optionalValue<int>(json, "maxRank");
    item.maxCharges = optionalValue<int>(json, "maxCharges");
    item.maxAmberStars = optionalValue<int>(json, "maxAmberStars");
    item.maxCyanStars = optionalValue<int>(json, "maxCyanStars");
    item.setRoot = optionalValue<bool>(json, "setRoot");
    item.tradingTax = optionalValue<int>(json, "tradingTax");

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

    // The display name is nested per language; prefer English, then whatever
    // the server sent first.
    if (const auto i18n = json.find("i18n"); i18n != json.end() && i18n->is_object())
    {
        if (const auto english = i18n->find("en");
            english != i18n->end() && english->is_object() && hasKey(*english, "name"))
        {
            item.itemName = english->at("name").get<std::string>();
        }
        else if (!i18n->empty() && i18n->begin().value().is_object() &&
                 hasKey(i18n->begin().value(), "name"))
        {
            item.itemName = i18n->begin().value().at("name").get<std::string>();
        }
    }

    return item;
}

/**
 * @brief Serialise an item in the shape `parseItem` reads back.
 *
 * Used by the item cache: writing the API's own field names means the load path
 * reuses `parseItem` instead of a second mapping that could drift from it.
 * Optional metadata is written only when it holds a value, because "absent" and
 * "zero" are not the same thing.
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
    writeOptional(out, "bulkTradable", item.bulkTradable);
    writeOptional(out, "maxRank", item.maxRank);
    writeOptional(out, "maxCharges", item.maxCharges);
    writeOptional(out, "maxAmberStars", item.maxAmberStars);
    writeOptional(out, "maxCyanStars", item.maxCyanStars);
    if (!item.subtypes.empty())
    {
        out["subtypes"] = item.subtypes;
    }
    writeOptional(out, "setRoot", item.setRoot);
    writeOptional(out, "tradingTax", item.tradingTax);
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
    user.ingameName = optionalValue<std::string>(json, "ingameName").value_or("");
    user.slug = optionalValue<std::string>(json, "slug").value_or("");
    user.platform = optionalValue<std::string>(json, "platform").value_or("");
    user.locale = optionalValue<std::string>(json, "locale").value_or("");
    user.status = optionalValue<std::string>(json, "status").value_or("");
    user.lastSeen = optionalValue<std::string>(json, "lastSeen").value_or("");
    user.reputation = optionalValue<long>(json, "reputation").value_or(0);
    user.crossplay = optionalValue<bool>(json, "crossplay").value_or(false);

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
    order.orderType = optionalValue<std::string>(json, "type").value_or("");
    order.platinum = optionalValue<int>(json, "platinum").value_or(0);
    order.quantity = optionalValue<int>(json, "quantity").value_or(0);
    order.visible = optionalValue<bool>(json, "visible").value_or(false);
    order.itemId = optionalValue<std::string>(json, "itemId").value_or("");
    order.createdAt = optionalValue<std::string>(json, "createdAt").value_or("");
    order.updatedAt = optionalValue<std::string>(json, "updatedAt").value_or("");

    order.perTrade = optionalValue<int>(json, "perTrade");
    order.subtype = optionalValue<std::string>(json, "subtype");
    order.rank = optionalValue<int>(json, "rank");
    order.charges = optionalValue<int>(json, "charges");
    order.amberStars = optionalValue<int>(json, "amberStars");
    order.cyanStars = optionalValue<int>(json, "cyanStars");

    if (hasKey(json, "user") && json.at("user").is_object())
    {
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
 * Takes the context as a `std::string_view`: GCC's -Wdangling-reference cannot
 * tell that the result outlives a call whose `const std::string &` argument
 * might be a temporary, and treating that warning as an error is the point of
 * the CI configuration.
 */
inline const nlohmann::json &requireDataArray(const nlohmann::json &root, std::string_view context)
{
    if (!root.is_object() || !root.contains("data") || !root.at("data").is_array())
    {
        throw std::runtime_error(std::string(context) + ": invalid API response format");
    }
    return root.at("data");
}

/**
 * @brief Extract the `data` object from a v2 envelope.
 * @return The object; reference is owned by `root`.
 * @throws std::runtime_error when `data` is missing or not an object.
 */
inline const nlohmann::json &requireDataObject(const nlohmann::json &root,
                                               std::string_view context)
{
    if (!root.is_object() || !root.contains("data") || !root.at("data").is_object())
    {
        throw std::runtime_error(std::string(context) + ": invalid API response format");
    }
    return root.at("data");
}

} // namespace wfm_json

#endif // WFM_JSON_H
