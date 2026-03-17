#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

// ==================== API Base URLs ====================
static const std::string V1_API = "https://api.warframe.market/v1";
static const std::string V2_API = "https://api.warframe.market/v2";

// ==================== Item Database ====================
// Efficient in-memory lookup tables used by both modern and legacy clients.
struct ItemDatabase
{
    std::unordered_map<std::string, std::string> idToSlug;      // O(1): id -> slug
    std::unordered_map<std::string, std::string> slugToId;      // O(1): slug -> id
    std::vector<std::string> allSlugs;                           // All slugs for iteration/substring search

    // Search for slug(s) containing input as substring
    std::vector<std::string> searchByName(const std::string& input) const
    {
        std::vector<std::string> results;
        for (const auto& slug : allSlugs)
        {
            if (slug.find(input) != std::string::npos)
                results.push_back(slug);
        }
        return results;
    }

    // Get slug from item ID - O(1) lookup
    std::string getSlugFromId(const std::string& id) const
    {
        auto it = idToSlug.find(id);
        if (it != idToSlug.end())
            return it->second;
        throw std::runtime_error("Item ID not found: " + id);
    }

    // Get item ID from slug - O(1) lookup
    std::string getIdFromSlug(const std::string& slug) const
    {
        auto it = slugToId.find(slug);
        if (it != slugToId.end())
            return it->second;
        throw std::runtime_error("Item slug not found: " + slug);
    }

    // Check if item exists by slug
    bool hasSlug(const std::string& slug) const
    {
        return slugToId.find(slug) != slugToId.end();
    }

    // Check if item exists by ID
    bool hasId(const std::string& id) const
    {
        return idToSlug.find(id) != idToSlug.end();
    }

    // Get total number of items
    size_t size() const
    {
        return allSlugs.size();
    }
};

extern ItemDatabase itemDB;

#endif // CONSTANTS_H
