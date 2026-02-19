#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

struct AuthResponse
{
    std::string token;
    std::string user_id;
    std::string ingame_name;
};

struct Item
{
    std::string id;
    std::string slug;
    std::string itemName;
    std::string gameRef;
    double avgVolume48h = 0.0;
    double avgMinPrice48h = 0.0;
};

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

#endif // TYPES_H
