#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "order.h"

// One executed match between a buy order and a sell order.
struct Trade {
    std::string symbol;
    Price price;                 // cents; always the resting order's price
    Quantity quantity;
    std::uint64_t buy_order_id;
    std::uint64_t sell_order_id;
    int buyer_id;
    int seller_id;
};

// Order book for ONE stock. Price-time priority:
//   1. better price matches first (highest bid / lowest ask)
//   2. at the same price, the order that arrived first (lowest sequence) matches first
//
// Not thread-safe on its own: have a single engine thread own each book and
// feed it orders through a queue.
class OrderBook {
public:
    explicit OrderBook(std::string symbol);

    // Matches the order against the opposite side. Any unfilled remainder of a
    // LIMIT order rests in the book; any unfilled remainder of a MARKET order
    // is discarded (immediate-or-cancel). Returns the trades that occurred.
    // Throws std::invalid_argument if the order is for a different symbol.
    std::vector<Trade> addOrder(std::unique_ptr<Order> order);

    // Removes a resting order. Returns false if the id isn't in the book.
    bool cancelOrder(std::uint64_t order_id);

    const std::string& getSymbol() const;
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t restingOrderCount() const;

    // Aggregated (price, total quantity) levels, best price first.
    // Useful for printing and for the GUI's order book table.
    std::vector<std::pair<Price, Quantity>> bidLevels(std::size_t depth) const;
    std::vector<std::pair<Price, Quantity>> askLevels(std::size_t depth) const;

    void printOrderBook(std::size_t depth = 5) const;

private:
    // FIFO queue of orders at one price. std::list keeps iterators valid when
    // other elements are erased, which makes cancel O(1) via the index below.
    using Level = std::list<std::unique_ptr<Order>>;

    // bids: highest price first. asks: lowest price first.
    std::map<Price, Level, std::greater<Price>> bids;
    std::map<Price, Level, std::less<Price>> asks;

    struct Location {
        OrderSide side;
        Price price;
        Level::iterator it;
    };
    std::unordered_map<std::uint64_t, Location> index;

    std::string symbol;

    // Match `incoming` against one side of the book (the opposite of incoming).
    template <typename BookSide>
    void matchAgainst(Order& incoming, BookSide& opposite,
                      std::vector<Trade>& trades);

    void rest(std::unique_ptr<Order> order);
};

#endif