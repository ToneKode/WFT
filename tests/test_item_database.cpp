#include <doctest.h>
#include "constants.h"

// ItemDatabase is the piece of the library with real logic and no I/O, so it
// gets direct coverage: lookups, missing-key behaviour, substring search, and
// the metadata that order validation reads.

namespace
{

Item makeItem(const std::string &id, const std::string &slug)
{
    Item item;
    item.id = id;
    item.slug = slug;
    return item;
}

} // namespace

TEST_CASE("add() indexes an item for id and slug lookups")
{
    ItemDatabase db;
    db.add(makeItem("54a73e65b5e5d8d3f0e7d4a6", "khora_prime_set"));

    CHECK(db.getSlugFromId("54a73e65b5e5d8d3f0e7d4a6") == "khora_prime_set");
    CHECK(db.getIdFromSlug("khora_prime_set") == "54a73e65b5e5d8d3f0e7d4a6");
    CHECK(db.size() == 1);
}

TEST_CASE("add() of an existing slug replaces it without duplicating the slug list")
{
    ItemDatabase db;
    Item item = makeItem("id-1", "khora_prime_set");
    db.add(item);

    item.hasMaxRank = true;
    item.maxRank = 5;
    db.add(item);

    CHECK(db.size() == 1);
    CHECK(db.getItem("khora_prime_set").maxRank == 5);
}

TEST_CASE("clear() empties every index")
{
    ItemDatabase db;
    db.add(makeItem("id-1", "khora_prime_set"));
    db.clear();

    CHECK(db.size() == 0);
    CHECK_FALSE(db.hasSlug("khora_prime_set"));
    CHECK_FALSE(db.hasId("id-1"));
    CHECK_THROWS_AS(db.getItem("khora_prime_set"), std::runtime_error);
}

TEST_CASE("missing keys throw rather than returning empty strings")
{
    ItemDatabase db;

    CHECK_THROWS_AS(db.getSlugFromId("does-not-exist"), std::runtime_error);
    CHECK_THROWS_AS(db.getIdFromSlug("does-not-exist"), std::runtime_error);
    CHECK_THROWS_WITH(db.getSlugFromId("does-not-exist"),
                      doctest::Contains("Item ID not found"));
    CHECK_THROWS_WITH(db.getIdFromSlug("does-not-exist"),
                      doctest::Contains("Item slug not found"));
}

TEST_CASE("existence checks do not throw on misses")
{
    ItemDatabase db;
    db.add(makeItem("id-khora", "khora_prime_set"));

    CHECK(db.hasSlug("khora_prime_set"));
    CHECK_FALSE(db.hasSlug("nope"));
    CHECK(db.hasId("id-khora"));
    CHECK_FALSE(db.hasId("nope"));
}

TEST_CASE("searchByName matches substrings and returns every hit")
{
    ItemDatabase db;
    db.add(makeItem("id-1", "khora_prime_set"));
    db.add(makeItem("id-2", "khora_prime_chassis_blueprint"));
    db.add(makeItem("id-3", "ash_prime_set"));

    const auto prime = db.searchByName("prime");
    CHECK(prime.size() == 3);

    const auto khora = db.searchByName("khora");
    CHECK(khora.size() == 2);
    CHECK(khora[0] == "khora_prime_set");

    CHECK(db.searchByName("nothing-matches").empty());
}

TEST_CASE("empty search matches everything, empty database matches nothing")
{
    ItemDatabase db;
    CHECK(db.searchByName("").empty());
    CHECK(db.size() == 0);

    db.add(makeItem("id-a", "a"));
    db.add(makeItem("id-b", "b"));
    CHECK(db.searchByName("").size() == 2);
}
