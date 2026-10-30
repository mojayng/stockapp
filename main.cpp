#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "order.h"
#include "orderbook.h"

namespace {

const char* sideName(OrderSide s) { return s == OrderSide::Buy ? "BUY " : "SELL"; }

void printTrades(const std::vector<Trade>& trades) {
    if (trades.empty()) {
        std::cout << "  (no trades)\n";
        return;
    }
    for (const auto& t : trades) {
        std::cout << "  TRADE " << t.symbol << " " << t.quantity << " @ $"
                  << std::fixed << std::setprecision(2) << centsToDollars(t.price)
                  << "  (buyer " << t.buyer_id << ", seller " << t.seller_id << ")\n";
    }
}

// Submit a limit order and show what happened.
std::uint64_t limit(OrderBook& book, int trader, OrderSide side, int qty, double price) {
    auto order = std::make_unique<LimitOrder>(trader, book.getSymbol(), side, qty,
                                              dollarsToCents(price));
    const std::uint64_t id = order->getID();
    std::cout << "\n> trader " << trader << " LIMIT " << sideName(side) << " " << qty
              << " @ $" << std::fixed << std::setprecision(2) << price << "\n";
    printTrades(book.addOrder(std::move(order)));
    return id;
}

// Submit a market order and show what happened.
void market(OrderBook& book, int trader, OrderSide side, int qty) {
    auto order = std::make_unique<MarketOrder>(trader, book.getSymbol(), side, qty);
    std::cout << "\n> trader " << trader << " MARKET " << sideName(side) << " " << qty
              << "\n";
    printTrades(book.addOrder(std::move(order)));
}

}  // namespace

int main() {
    OrderBook book("AAPL");

    std::cout << "---- Step 1: traders post resting orders ----";
    limit(book, 1, OrderSide::Sell, 50, 101.00);   // oldest seller at 101
    limit(book, 2, OrderSide::Sell, 50, 101.00);   // second seller at 101
    limit(book, 3, OrderSide::Sell, 100, 102.00);
    const auto cancel_me = limit(book, 4, OrderSide::Buy, 30, 99.00);
    limit(book, 5, OrderSide::Buy, 20, 98.50);
    std::cout << "\n";
    book.printOrderBook();

    std::cout << "\n---- Step 2: time priority (trader 6 buys 70 @ $101) ----";
    limit(book, 6, OrderSide::Buy, 70, 101.00);    // fills trader 1, then part of trader 2
    std::cout << "\n";
    book.printOrderBook();

    std::cout << "\n---- Step 3: market order sweeps price levels ----";
    market(book, 7, OrderSide::Buy, 150);          // takes 30 @ 101, 100 @ 102, rest dropped
    std::cout << "\n";
    book.printOrderBook();

    std::cout << "\n---- Step 4: cancel a resting order ----\n";
    std::cout << "cancel order " << cancel_me << ": "
              << (book.cancelOrder(cancel_me) ? "ok" : "not found") << "\n";
    book.printOrderBook();

    std::cout << "\nresting orders left: " << book.restingOrderCount() << "\n";
    return 0;
}