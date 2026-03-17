#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

// ==================== Authentication ====================
struct AuthResponse
{
    std::string token;
    std::string user_id;
    std::string ingame_name;
};

// ==================== Market Data ====================
struct Item
{
    std::string id;
    std::string slug;
    std::string itemName;
    std::string gameRef;
    double avgVolume48h = 0.0;
    double avgMinPrice48h = 0.0;
};

// ==================== Order Models ====================
struct Order
{
    std::string id;
    std::string orderType; // "buy" or "sell"
    int platinum;
    int quantity;
    std::string userName;
    std::string userStatus; // "offline", "online", "ingame"
    std::string platform;   // "pc"
    bool visible;
};

// API response model for authenticated order listing.
struct OrderDetail
{
    std::string id;
    std::string orderType; // "buy" or "sell"
    int platinum;
    int quantity;
    std::string itemId;
    std::string itemSlug;
    bool visible;
};

// C++14-friendly partial update request for PATCH /order/{id}.
struct UpdateOrderRequest
{
    bool hasPlatinum = false;
    int platinum = 0;

    bool hasQuantity = false;
    int quantity = 0;

    bool hasVisible = false;
    bool visible = true;

    bool hasRank = false;
    int rank = 0;

    bool hasChanges() const
    {
        return hasPlatinum || hasQuantity || hasVisible || hasRank;
    }
};

// C++14-friendly order creation request for POST /order/.
struct SellOrderRequest
{
    std::string itemSlug;
    int platinum = 0;
    int quantity = 1;
    bool visible = true;

    bool hasRank = false;
    int rank = 0;
};

#endif // TYPES_H
