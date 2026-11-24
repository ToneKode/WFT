#ifndef ORDER_VALIDATION_H
#define ORDER_VALIDATION_H

#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include "types.h"

/**
 * @file order_validation.h
 * @brief Builds and checks order request bodies against the v2 contract.
 *
 * The API rejects fields that do not apply to an item and rejects orders that
 * omit fields the item requires. Both halves are documented per field, for
 * example:
 *
 * - `perTrade` is "Required for bulk-tradable items, forbidden otherwise"
 * - `rank` is "Required when the item supports ranks, forbidden otherwise"
 * - `subtype` is "Required when the item has subtypes, forbidden otherwise"
 *
 * Sending the wrong shape costs a round trip and returns a terse error code,
 * so requests are checked against the item's own metadata before they are
 * sent. `ValidationError` carries the offending field so a caller can point at
 * the mistake instead of retrying blindly.
 */

namespace order_validation
{

/**
 * @brief Order request that does not match the item it targets.
 */
class ValidationError : public std::invalid_argument
{
public:
    ValidationError(const std::string &field, const std::string &message)
        : std::invalid_argument(field + ": " + message), field_(field) {}

    /// The request field at fault, e.g. "perTrade".
    const std::string &field() const { return field_; }

private:
    std::string field_;
};

inline const char *orderTypeToString(OrderType type)
{
    return type == OrderType::Buy ? "buy" : "sell";
}

/**
 * @brief Reject a create request whose optional fields do not fit the item.
 *
 * @param request Request to check.
 * @param item Metadata for the item being traded.
 * @throws ValidationError on the first rule the request violates.
 */
inline void validateAgainst(const PlaceOrderRequest &request, const Item &item)
{
    if (request.itemSlug.empty())
    {
        throw ValidationError("itemSlug", "item slug is required");
    }
    if (request.platinum < 1 || request.platinum > 900000)
    {
        throw ValidationError("platinum", "must be between 1 and 900000");
    }
    if (request.quantity < 1 || request.quantity > 9999)
    {
        throw ValidationError("quantity", "must be between 1 and 9999");
    }

    if (item.requiresPerTrade())
    {
        if (!request.hasPerTrade)
        {
            throw ValidationError("perTrade", "required for bulk-tradable item");
        }
        if (request.perTrade < 1 || request.perTrade > 6)
        {
            throw ValidationError("perTrade", "must be between 1 and 6");
        }
        if (request.quantity % request.perTrade != 0)
        {
            throw ValidationError("perTrade", "must divide quantity");
        }
    }
    else if (request.hasPerTrade)
    {
        throw ValidationError("perTrade", "only allowed for bulk-tradable items");
    }

    if (item.requiresRank())
    {
        if (!request.hasRank)
        {
            throw ValidationError("rank", "required for ranked item");
        }
        if (request.rank < 0 || request.rank > item.maxRank)
        {
            throw ValidationError("rank", "must be between 0 and " +
                                               std::to_string(item.maxRank));
        }
    }
    else if (request.hasRank)
    {
        throw ValidationError("rank", "only allowed for items that support ranks");
    }

    if (item.hasMaxCharges)
    {
        if (!request.hasCharges)
        {
            throw ValidationError("charges", "required for item with charges");
        }
        if (request.charges < 0 || request.charges > item.maxCharges)
        {
            throw ValidationError("charges", "must be between 0 and " +
                                                 std::to_string(item.maxCharges));
        }
    }
    else if (request.hasCharges)
    {
        throw ValidationError("charges", "only allowed for items that support charges");
    }

    if (item.requiresSubtype())
    {
        if (!request.hasSubtype)
        {
            throw ValidationError("subtype", "required for item with subtypes");
        }
        bool known = false;
        for (const auto &subtype : item.subtypes)
        {
            if (subtype == request.subtype)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            throw ValidationError("subtype", "is not one of the item's subtypes");
        }
    }
    else if (request.hasSubtype)
    {
        throw ValidationError("subtype", "only allowed for items with subtypes");
    }

    if (item.hasMaxAmberStars && item.maxAmberStars > 0)
    {
        if (!request.hasAmberStars)
        {
            throw ValidationError("amberStars", "required for item with amber stars");
        }
        if (request.amberStars < 0 || request.amberStars > item.maxAmberStars)
        {
            throw ValidationError("amberStars", "must be between 0 and " +
                                                    std::to_string(item.maxAmberStars));
        }
    }
    else if (request.hasAmberStars)
    {
        throw ValidationError("amberStars", "not allowed for this item");
    }

    if (item.hasMaxCyanStars && item.maxCyanStars > 0)
    {
        if (!request.hasCyanStars)
        {
            throw ValidationError("cyanStars", "required for item with cyan stars");
        }
        if (request.cyanStars < 0 || request.cyanStars > item.maxCyanStars)
        {
            throw ValidationError("cyanStars", "must be between 0 and " +
                                                   std::to_string(item.maxCyanStars));
        }
    }
    else if (request.hasCyanStars)
    {
        throw ValidationError("cyanStars", "not allowed for this item");
    }
}

/**
 * @brief Serialise a validated create request into the API's body shape.
 *
 * Only fields the caller set are emitted, so an inapplicable field is never
 * sent even if the request struct happens to carry a stale value.
 */
inline nlohmann::json buildPayload(const PlaceOrderRequest &request,
                                  const std::string &itemId)
{
    nlohmann::json payload = {
        {"itemId", itemId},
        {"type", orderTypeToString(request.type)},
        {"platinum", request.platinum},
        {"quantity", request.quantity},
        {"visible", request.visible}};

    if (request.hasPerTrade)
    {
        payload["perTrade"] = request.perTrade;
    }
    if (request.hasRank)
    {
        payload["rank"] = request.rank;
    }
    if (request.hasCharges)
    {
        payload["charges"] = request.charges;
    }
    if (request.hasSubtype)
    {
        payload["subtype"] = request.subtype;
    }
    if (request.hasAmberStars)
    {
        payload["amberStars"] = request.amberStars;
    }
    if (request.hasCyanStars)
    {
        payload["cyanStars"] = request.cyanStars;
    }

    return payload;
}

/**
 * @brief Serialise a partial update, emitting only the fields that were set.
 */
inline nlohmann::json buildPatchPayload(const UpdateOrderRequest &request)
{
    nlohmann::json payload = nlohmann::json::object();

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
    if (request.hasPerTrade)
    {
        payload["perTrade"] = request.perTrade;
    }
    if (request.hasCharges)
    {
        payload["charges"] = request.charges;
    }
    if (request.hasSubtype)
    {
        payload["subtype"] = request.subtype;
    }
    if (request.hasAmberStars)
    {
        payload["amberStars"] = request.amberStars;
    }
    if (request.hasCyanStars)
    {
        payload["cyanStars"] = request.cyanStars;
    }

    return payload;
}

} // namespace order_validation

#endif // ORDER_VALIDATION_H
