#include "order.h"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace {
// Global counters. Atomic so orders can be created from multiple threads.
// The sequence number replaces a seconds-resolution timestamp: two orders
// created in the same second still get a strict, unambiguous time priority.
std::atomic<std::uint64_t> next_order_id{1};
std::atomic<std::uint64_t> next_sequence{1};
}

Price dollarsToCents(double dollars) {
    // round to nearest cent (half away from zero)
    return static_cast<Price>(dollars * 100.0 + (dollars >= 0 ? 0.5 : -0.5));
}

double centsToDollars(Price cents) {
    return static_cast<double>(cents) / 100.0;
}

Order::Order(int trader_id, std::string symbol, OrderSide side,
             Quantity quantity, Price price)
    : id(next_order_id.fetch_add(1)),
      sequence(next_sequence.fetch_add(1)),
      trader_id(trader_id),
      symbol(std::move(symbol)),
      side(side),
      quantity(quantity),
      price(price) {
    if (quantity <= 0) {
        throw std::invalid_argument("Order quantity must be positive");
    }
    if (price < 0) {
        throw std::invalid_argument("Order price cannot be negative");
    }
}

std::uint64_t Order::getID() const { return id; }
std::uint64_t Order::getSequence() const { return sequence; }
int Order::getTraderID() const { return trader_id; }
const std::string& Order::getSymbol() const { return symbol; }
OrderSide Order::getOrderSide() const { return side; }
Quantity Order::getQuantity() const { return quantity; }
Price Order::getPrice() const { return price; }

bool Order::isFilled() const { return quantity == 0; }

void Order::reduceQuantity(Quantity amount) {
    if (amount <= 0 || amount > quantity) {
        throw std::invalid_argument("Invalid fill amount");
    }
    quantity -= amount;
}

LimitOrder::LimitOrder(int trader_id, std::string symbol, OrderSide side,
                       Quantity quantity, Price limit_price)
    : Order(trader_id, std::move(symbol), side, quantity, limit_price) {
    if (limit_price <= 0) {
        throw std::invalid_argument("Limit price must be positive");
    }
}

std::string LimitOrder::getOrderType() const { return "LIMIT"; }

MarketOrder::MarketOrder(int trader_id, std::string symbol, OrderSide side,
                         Quantity quantity)
    : Order(trader_id, std::move(symbol), side, quantity, 0) {}

std::string MarketOrder::getOrderType() const { return "MARKET"; }