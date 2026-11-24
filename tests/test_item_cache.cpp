#include <doctest.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "fake_transport.h"
#include "item_cache.h"
#include "market_api.h"

// The item cache is what stops a trading tool re-fetching ~3,900 items on every
// start, and versions() is the cheap check that decides whether it must.

namespace
{

std::unique_ptr<FakeTransport> makeTransport(FakeTransport *&out)
{
    std::unique_ptr<FakeTransport> transport(new FakeTransport());
    out = transport.get();
    return transport;
}

nlohmann::json versionsJson(const std::string &itemsHash)
{
    return {{"apiVersion", "0.25.0"},
            {"data",
             {{"collections", {{"items", itemsHash}, {"rivens", "other"}}},
              {"updatedAt", "2026-10-28T03:52:09Z"}}},
            {"error", nullptr}};
}

/// A path in the CWD with a name unlikely to collide with a real file.
std::string tempPath(const std::string &name)
{
    return "wft_test_" + name + ".json";
}

/// Remove a temp file without failing when it was never created.
void removeFile(const std::string &path)
{
    std::remove(path.c_str());
}

nlohmann::json cachedItemJson(const std::string &slug, int maxRank)
{
    return {{"id", "id-" + slug},
            {"slug", slug},
            {"tradable", true},
            {"maxRank", maxRank},
            {"bulkTradable", true},
            {"tradingTax", 4000},
            {"i18n", {{"en", {{"name", "Name of " + slug}}}}}};
}

} // namespace

TEST_CASE("versions() exposes the items hash and updatedAt")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson(versionsJson("MjAyNi0xMC0yOFQwMzo1MjowOQ=="));

    const MarketAPI::Versions versions = api.versions();

    CHECK(versions.itemsHash == "MjAyNi0xMC0yOFQwMzo1MjowOQ==");
    CHECK(versions.updatedAt == "2026-10-28T03:52:09Z");
    CHECK(fake->requests().at(0).url == V2_API + "/versions");
}

TEST_CASE("versions() tolerates a missing collections object")
{
    FakeTransport *fake = nullptr;
    MarketAPI api(makeTransport(fake));

    fake->queueJson({{"data", {{"updatedAt", "2026-10-28T03:52:09Z"}}}, {"error", nullptr}});

    // A shape change should not throw: an empty hash just means "refresh".
    const MarketAPI::Versions versions = api.versions();
    CHECK(versions.itemsHash.empty());
    CHECK(versions.updatedAt == "2026-10-28T03:52:09Z");
}

TEST_CASE("the item cache round-trips every field order validation needs")
{
    const std::string path = tempPath("roundtrip");
    removeFile(path);

    std::vector<Item> saved;
    saved.push_back(wfm_json::parseItem(cachedItemJson("khora_prime_set", 0)));
    saved.push_back(wfm_json::parseItem(
        {{"id", "i2"},
         {"slug", "axi_a1_relic"},
         {"tradable", true},
         {"bulkTradable", true},
         {"subtypes", nlohmann::json::array({"intact", "exceptional"})},
         {"maxAmberStars", 3},
         {"maxCyanStars", 2},
         {"setRoot", true}}));

    item_cache::saveItems(path, saved, "hash-1");

    std::string hash;
    const std::vector<Item> loaded = item_cache::loadItems(path, hash);
    removeFile(path);

    CHECK(hash == "hash-1");
    REQUIRE(loaded.size() == 2);

    CHECK(loaded[0].slug == "khora_prime_set");
    CHECK(loaded[0].id == "id-khora_prime_set");
    CHECK(loaded[0].itemName == "Name of khora_prime_set");
    CHECK(loaded[0].requiresRank());
    CHECK(loaded[0].requiresPerTrade());
    CHECK(loaded[0].hasTradingTax);

    CHECK(loaded[1].slug == "axi_a1_relic");
    REQUIRE(loaded[1].subtypes.size() == 2);
    CHECK(loaded[1].subtypes[1] == "exceptional");
    CHECK(loaded[1].requiresStars());
    CHECK(loaded[1].hasMaxAmberStars);
    CHECK(loaded[1].maxAmberStars == 3);
    CHECK(loaded[1].hasSetRoot);
    CHECK(loaded[1].setRoot);
}

TEST_CASE("a missing cache file reports the path instead of returning nothing")
{
    const std::string path = tempPath("missing");
    removeFile(path);

    std::string hash;
    CHECK_THROWS_WITH(item_cache::loadItems(path, hash),
                      doctest::Contains("cannot open"));
}

TEST_CASE("a corrupt cache file fails loudly rather than loading empty")
{
    const std::string path = tempPath("corrupt");
    {
        // Valid JSON, wrong shape: the silent-empty-cache trap.
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "{\"items\": {}}";
    }

    std::string hash;
    CHECK_THROWS_WITH(item_cache::loadItems(path, hash),
                      doctest::Contains("not an item cache"));
    removeFile(path);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "not json";
    }
    CHECK_THROWS_WITH(item_cache::loadItems(path, hash),
                      doctest::Contains("Failed to read item cache"));
    removeFile(path);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "{\"items\": []}";
    }
    CHECK_THROWS_WITH(item_cache::loadItems(path, hash), doctest::Contains("no items"));
    removeFile(path);
}

TEST_CASE("a cache entry missing id or slug names the cache file")
{
    const std::string path = tempPath("badentry");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << nlohmann::json{{"itemsHash", "h"},
                               {"items", nlohmann::json::array({{{"slug", "no_id"}}})}}
                    .dump();
    }

    std::string hash;
    CHECK_THROWS_WITH(item_cache::loadItems(path, hash),
                      doctest::Contains("Failed to read item cache"));
    removeFile(path);
}

