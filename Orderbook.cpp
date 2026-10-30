#include "orderbook.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>

OrderBook::OrderBook(std::string symbol) : symbol(std::move(symbol)) {}

const std::string& OrderBook::getSymbol() const { return symbol; }

std::optional<Price> OrderBook::bestBid() const {
    if (bids.empty()) return std::nullopt;
    return bids.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks.empty()) return std::nullopt;
    return asks.begin()->first;
}

std::size_t OrderBook::restingOrderCount() const { return index.size(); }

// Walks the opposite side from best price outward, filling `incoming` until it
// is done, the book side is empty, or the next price no longer crosses.
template <typename BookSide>
void OrderBook::matchAgainst(Order& incoming, BookSide& opposite,
                             std::vector<Trade>& trades) {
    const bool incoming_is_buy = incoming.getOrderSide() == OrderSide::Buy;
    const bool is_market = incoming.getOrderType() == "MARKET";

    while (!incoming.isFilled() && !opposite.empty()) {
        auto level_it = opposite.begin();  // best price on that side
        const Price level_price = level_it->first;

        // A limit order only trades at its price or better.
        if (!is_market) {
            const bool crosses = incoming_is_buy ? (level_price <= incoming.getPrice())
                                                 : (level_price >= incoming.getPrice());
            if (!crosses) break;
        }

        Level& queue = level_it->second;
        while (!incoming.isFilled() && !queue.empty()) {
            Order& resting = *queue.front();  // oldest order at this price
            const Quantity fill = std::min(incoming.getQuantity(), resting.getQuantity());

            const Order& buy = incoming_is_buy ? incoming : resting;
            const Order& sell = incoming_is_buy ? resting : incoming;
            trades.push_back(Trade{symbol, level_price, fill, buy.getID(),
                                   sell.getID(), buy.getTraderID(), sell.getTraderID()});

            incoming.reduceQuantity(fill);
            resting.reduceQuantity(fill);

            if (resting.isFilled()) {
                index.erase(resting.getID());
                queue.pop_front();
            }
        }

        if (queue.empty()) {
            opposite.erase(level_it);
        }
    }
}

void OrderBook::rest(std::unique_ptr<Order> order) {
    const std::uint64_t id = order->getID();
    const Price price = order->getPrice();
    const OrderSide side = order->getOrderSide();

    if (side == OrderSide::Buy) {
        Level& level = bids[price];
        level.push_back(std::move(order));
        index[id] = Location{side, price, std::prev(level.end())};
    } else {
        Level& level = asks[price];
        level.push_back(std::move(order));
        index[id] = Location{side, price, std::prev(level.end())};
    }
}

std::vector<Trade> OrderBook::addOrder(std::unique_ptr<Order> order) {
    if (!order) {
        throw std::invalid_argument("Null order");
    }
    if (order->getSymbol() != symbol) {
        throw std::invalid_argument("Order symbol does not match this book");
    }

    std::vector<Trade> trades;

    if (order->getOrderSide() == OrderSide::Buy) {
        matchAgainst(*order, asks, trades);
    } else {
        matchAgainst(*order, bids, trades);
    }

    // Leftover limit quantity rests in the book. Leftover market quantity is dropped.
    if (!order->isFilled() && order->getOrderType() == "LIMIT") {
        rest(std::move(order));
    }
    return trades;
}

bool OrderBook::cancelOrder(std::uint64_t order_id) {
    auto found = index.find(order_id);
    if (found == index.end()) return false;

    const Location loc = found->second;
    if (loc.side == OrderSide::Buy) {
        auto level_it = bids.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) bids.erase(level_it);
    } else {
        auto level_it = asks.find(loc.price);
        level_it->second.erase(loc.it);
        if (level_it->second.empty()) asks.erase(level_it);
    }
    index.erase(found);
    return true;
}

namespace {
template <typename BookSide>
std::vector<std::pair<Price, Quantity>> collectLevels(const BookSide& side,
                                                      std::size_t depth) {
    std::vector<std::pair<Price, Quantity>> out;
    for (const auto& [price, queue] : side) {
        if (out.size() == depth) break;
        Quantity total = 0;
        for (const auto& o : queue) total += o->getQuantity();
        out.emplace_back(price, total);
    }
    return out;
}
}  // namespace

std::vector<std::pair<Price, Quantity>> OrderBook::bidLevels(std::size_t depth) const {
    return collectLevels(bids, depth);
}

std::vector<std::pair<Price, Quantity>> OrderBook::askLevels(std::size_t depth) const {
    return collectLevels(asks, depth);
}

void OrderBook::printOrderBook(std::size_t depth) const {
    std::cout << "=== " << symbol << " ===\n";
    std::cout << std::fixed << std::setprecision(2);

    // Print asks highest-to-lowest so the best ask sits right above the best bid.
    auto ask = askLevels(depth);
    for (auto it = ask.rbegin(); it != ask.rend(); ++it) {
        std::cout << "  ASK  " << std::setw(10) << centsToDollars(it->first)
                  << "  x " << it->second << "\n";
    }
    std::cout << "  ----------------------\n";
    for (const auto& [price, qty] : bidLevels(depth)) {
        std::cout << "  BID  " << std::setw(10) << centsToDollars(price)
                  << "  x " << qty << "\n";
    }
}