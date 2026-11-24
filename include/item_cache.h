#ifndef ITEM_CACHE_H
#define ITEM_CACHE_H

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "types.h"
#include "wfm_json.h"

/**
 * @file item_cache.h
 * @brief Save the item catalogue to disk and read it back.
 *
 * `MarketAPI::items()` returns ~3,900 items in one response. A tool that starts
 * often should not pay for that every time, and the API rules ask clients to
 * cache. `MarketAPI::versions()` reports the `collections.items` hash, which
 * changes only when the collection does, so the cheap check is: fetch versions,
 * compare with `itemsHash` from the cache file, and reload only on a mismatch.
 *
 * @code
 * std::vector<Item> items;
 * std::string cachedHash;
 * try
 * {
 *     items = item_cache::loadItems("items.json", cachedHash);
 * }
 * catch (const std::runtime_error &)
 * {
 *     // No usable cache; fall through and fetch.
 * }
 *
 * const MarketAPI::Versions versions = api.versions();
 * if (items.empty() || cachedHash != versions.itemsHash)
 * {
 *     items = api.items();
 *     item_cache::saveItems("items.json", items, versions.itemsHash);
 * }
 * @endcode
 *
 * The file is plain JSON in the API's own field names, so it is inspectable and
 * there is no format version to migrate: a shape change fails the same
 * `parseItem` the live response goes through.
 */

namespace item_cache
{

/**
 * @brief Write items to `path`, tagged with the collection hash they came from.
 * @param path Destination file; overwritten.
 * @param items Items from `MarketAPI::items()`.
 * @param itemsHash `collections.items` at fetch time; empty when unknown.
 * @throws std::runtime_error when the file cannot be written.
 */
inline void saveItems(const std::string &path, const std::vector<Item> &items,
                      const std::string &itemsHash)
{
    nlohmann::json entries = nlohmann::json::array();
    for (const auto &item : items)
    {
        entries.push_back(wfm_json::toItemJson(item));
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        throw std::runtime_error("Failed to write item cache " + path + ": cannot open");
    }

    file << nlohmann::json{{"itemsHash", itemsHash}, {"items", entries}}.dump();
    file.close();
    if (!file)
    {
        throw std::runtime_error("Failed to write item cache " + path + ": write failed");
    }
}

/**
 * @brief Read a cache file written by `saveItems()`.
 *
 * @param path Source file.
 * @param itemsHash Set to the recorded hash; empty when the file has none, which
 *        callers should treat as stale.
 * @return The items.
 * @throws std::runtime_error when the file is missing, is not an item cache, or
 *         holds no items.
 *
 * Corruption fails loudly on purpose: an empty result looks like a working
 * cache.
 */
inline std::vector<Item> loadItems(const std::string &path, std::string &itemsHash)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to read item cache " + path + ": cannot open");
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file);
    }
    catch (const nlohmann::json::parse_error &error)
    {
        throw std::runtime_error("Failed to read item cache " + path + ": " + error.what());
    }

    if (!root.is_object() || !wfm_json::hasKey(root, "items") || !root.at("items").is_array())
    {
        throw std::runtime_error("Failed to read item cache " + path + ": not an item cache");
    }
    if (root.at("items").empty())
    {
        throw std::runtime_error("Failed to read item cache " + path + ": no items");
    }

    itemsHash = (wfm_json::hasKey(root, "itemsHash") && root.at("itemsHash").is_string())
                    ? root.at("itemsHash").get<std::string>()
                    : std::string();

    std::vector<Item> items;
    items.reserve(root.at("items").size());
    for (const auto &entry : root.at("items"))
    {
        try
        {
            items.push_back(wfm_json::parseItem(entry));
        }
        catch (const std::runtime_error &error)
        {
            throw std::runtime_error("Failed to read item cache " + path + ": " + error.what());
        }
    }
    return items;
}

} // namespace item_cache

#endif // ITEM_CACHE_H
