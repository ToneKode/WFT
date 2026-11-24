#include <doctest.h>
#include "constants.h"

// ItemDatabase is the piece of the library with real logic and no I/O, so it
// gets direct coverage: lookups, missing-key behaviour, and substring search.

TEST_CASE("lookup tables round-trip between id and slug")
{
    ItemDatabase db;
    db.idToSlug["54a73e65b5e5d8d3f0e7d4a6"] = "khora_prime_set";
    db.slugToId["khora_prime_set"] = "54a73e65b5e5d8d3f0e7d4a6";
    db.allSlugs.push_back("khora_prime_set");

    CHECK(db.getSlugFromId("54a73e65b5e5d8d3f0e7d4a6") == "khora_prime_set");
    CHECK(db.getIdFromSlug("khora_prime_set") == "54a73e65b5e5d8d3f0e7d4a6");
    CHECK(db.size() == 1);
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
    db.slugToId["khora_prime_set"] = "id-khora";
    db.idToSlug["id-khora"] = "khora_prime_set";
    db.allSlugs.push_back("khora_prime_set");

    CHECK(db.hasSlug("khora_prime_set"));
    CHECK_FALSE(db.hasSlug("nope"));
    CHECK(db.hasId("id-khora"));
    CHECK_FALSE(db.hasId("nope"));
}

TEST_CASE("searchByName matches substrings and returns every hit")
{
    ItemDatabase db;
    db.allSlugs = {"khora_prime_set", "khora_prime_chassis_blueprint", "ash_prime_set"};

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

    db.allSlugs = {"a", "b"};
    CHECK(db.searchByName("").size() == 2);
}
