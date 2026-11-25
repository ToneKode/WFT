#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <vector>
#include "types.h"

/**
 * @file order_book.h
 * @brief Per-item order book, ranked by unit price.
 *
 * ## Why normalisation is not optional
 *
 * `Order::platinum` is the **total for a bundle of `perTrade` units**, not the
 * price of one unit. Live data shows it plainly: for one relic, buy orders
 * appear as `5 platinum / perTrade 1` and `30 platinum / perTrade 6`, which are
 * the same 5-per-relic offer.
 *
 * Ranking on raw `platinum` therefore compares bundles rather than units, and
 * makes bulk sellers look several times more expensive than they are. Every
 * comparison here goes through `platinumPerUnit()`.
 *
 * ## Ranking
 *
 * Matches the server's `/top` convention: sells cheapest-per-unit first (the
 * buyer's view), buys highest-per-unit first (the seller's), with offline users
 * excluded because they cannot trade. Orders with no embedded user stay: an
 * unknown owner is not offline.
 *
 * Endo and star items have extra server-side ranking rules that are not
 * replicated here; for those, prefer `/top`, which MarketAPI also exposes.
 */
class OrderBook
{
public:
    /**
     * @brief Index orders for one item.
     * @param orders Orders from the API; hidden and offline ones are dropped.
     */
    void load(const std::vector<Order> &orders)
    {
        sells_.clear();
        buys_.clear();

        for (const auto &order : orders)
        {
            if (!order.visible)
            {
                continue;
            }
            // A missing user means "unknown owner", not "offline".
            // Only an explicit non-online status filters.
            if (order.user && !order.user->isOnline())
            {
                continue;
            }
            if (order.orderType == "sell")
            {
                sells_.push_back(order);
            }
            else if (order.orderType == "buy")
            {
                buys_.push_back(order);
            }
        }

        // Deeper books win ties: an offer that can actually fill is better.
        std::ranges::stable_sort(sells_, cheaper);
        std::ranges::stable_sort(buys_, dearer);
    }

    /// Sell orders, cheapest per unit first.
    [[nodiscard]] const std::vector<Order> &sells() const { return sells_; }

    /// Buy orders, highest per unit first.
    [[nodiscard]] const std::vector<Order> &buys() const { return buys_; }

    /// Cheapest sell price per unit; `std::nullopt` when there are no sell orders.
    [[nodiscard]] std::optional<double> bestSell() const
    {
        if (sells_.empty())
        {
            return std::nullopt;
        }
        return sells_.front().platinumPerUnit();
    }

    /// Highest buy price per unit; `std::nullopt` when there are no buy orders.
    [[nodiscard]] std::optional<double> bestBuy() const
    {
        if (buys_.empty())
        {
            return std::nullopt;
        }
        return buys_.front().platinumPerUnit();
    }

    /**
     * @brief Best sell minus best buy, per unit.
     *
     * Zero or negative means the book is crossed, which normally indicates a
     * stale side rather than a free profit.
     *
     * @return `std::nullopt` when either side is empty.
     */
    [[nodiscard]] std::optional<double> spread() const
    {
        const std::optional<double> sell = bestSell();
        const std::optional<double> buy = bestBuy();
        if (!sell || !buy)
        {
            return std::nullopt;
        }
        return *sell - *buy;
    }

private:
    std::vector<Order> sells_;
    std::vector<Order> buys_;

    static bool cheaper(const Order &a, const Order &b)
    {
        const double pa = a.platinumPerUnit();
        const double pb = b.platinumPerUnit();
        return pa != pb ? pa < pb : a.quantity > b.quantity;
    }

    static bool dearer(const Order &a, const Order &b)
    {
        const double pa = a.platinumPerUnit();
        const double pb = b.platinumPerUnit();
        return pa != pb ? pa > pb : a.quantity > b.quantity;
    }
};

#endif // ORDER_BOOK_H
