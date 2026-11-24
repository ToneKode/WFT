#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include "types.h"

// ==================== API Base URLs ====================
static const std::string V1_API = "https://api.warframe.market/v1";
static const std::string V2_API = "https://api.warframe.market/v2";
static const std::string WS_API = "wss://ws.warframe.market/socket";

/**
 * @brief Version of this library, sent as the User-Agent.
 *
 * The Warframe.Market API rules require every client to identify itself and
 * give the operators a way to classify its traffic. Keep the contact URL
 * pointing at the repository so they can reach the maintainer.
 */
static const std::string WFT_USER_AGENT =
    "wft/0.2.0 (+https://github.com/tonekode/wft)";

// ==================== Item Database ====================
/**
 * @brief In-memory item index used for slug/id lookups and order validation.
 *
 * Order creation depends on per-item metadata (rank, subtype, bulk trading),
 * so the full Item is retained rather than just the id/slug pair.
 */
struct ItemDatabase
{
    std::unordered_map<std::string, std::string> idToSlug; // O(1): id -> slug
    std::unordered_map<std::string, std::string> slugToId; // O(1): slug -> id
    std::vector<std::string> allSlugs;                     // insertion order
    std::unordered_map<std::string, Item> itemsBySlug;     // full metadata

    /// Number of items indexed.
    size_t size() const { return allSlugs.size(); }

    /// Remove every item. Tests call this; a reload also implies it.
    void clear()
    {
        idToSlug.clear();
        slugToId.clear();
        allSlugs.clear();
        itemsBySlug.clear();
    }

    /// Index one item, replacing any previous entry for the same slug.
    void add(const Item &item)
    {
        if (itemsBySlug.find(item.slug) == itemsBySlug.end())
        {
            allSlugs.push_back(item.slug);
        }
        itemsBySlug[item.slug] = item;
        idToSlug[item.id] = item.slug;
        slugToId[item.slug] = item.id;
    }

    /// Search for slug(s) containing input as substring.
    std::vector<std::string> searchByName(const std::string &input) const
    {
        std::vector<std::string> results;
        for (const auto &slug : allSlugs)
        {
            if (slug.find(input) != std::string::npos)
            {
                results.push_back(slug);
            }
        }
        return results;
    }

    /// Get slug from item ID - O(1) lookup.
    std::string getSlugFromId(const std::string &id) const
    {
        auto it = idToSlug.find(id);
        if (it != idToSlug.end())
        {
            return it->second;
        }
        throw std::runtime_error("Item ID not found: " + id);
    }

    /// Get item ID from slug - O(1) lookup.
    std::string getIdFromSlug(const std::string &slug) const
    {
        auto it = slugToId.find(slug);
        if (it != slugToId.end())
        {
            return it->second;
        }
        throw std::runtime_error("Item slug not found: " + slug);
    }

    /**
     * @brief Get full item metadata by slug.
     * @throws std::runtime_error when the slug is not loaded.
     */
    const Item &getItem(const std::string &slug) const
    {
        auto it = itemsBySlug.find(slug);
        if (it != itemsBySlug.end())
        {
            return it->second;
        }
        throw std::runtime_error("Item slug not found: " + slug);
    }

    /// Check if item exists by slug.
    bool hasSlug(const std::string &slug) const
    {
        return slugToId.find(slug) != slugToId.end();
    }

    /// Check if item exists by ID.
    bool hasId(const std::string &id) const
    {
        return idToSlug.find(id) != idToSlug.end();
    }
};

extern ItemDatabase itemDB;

#endif // CONSTANTS_H
