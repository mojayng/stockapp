#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <string>

// Prices are stored as integer cents (e.g. $187.35 -> 18735).
// Floating-point prices cause equality/ordering bugs in a matching engine.
using Price = std::int64_t;
using Quantity = int;

// Convert a dollar amount to integer cents (rounds to nearest cent).
Price dollarsToCents(double dollars);
// Convert integer cents back to dollars (for display only).
double centsToDollars(Price cents);

// Which side of the book the order is on.
// (Renamed from OrderType so it doesn't clash with Market/Limit.)
enum class OrderSide {
    Buy,
    Sell
};

class Order {
private: // private so outside code can't modify directly
    std::uint64_t id;        // unique order id
    std::uint64_t sequence;  // strictly increasing; used for time priority
    int trader_id;
    std::string symbol;      // which stock this order is for
    OrderSide side;
    Quantity quantity;       // remaining (unfilled) quantity
    Price price;             // in cents; 0 for market orders (unused)

protected:
    // Only subclasses can construct an Order.
    Order(int trader_id, std::string symbol, OrderSide side,
          Quantity quantity, Price price);

public:
    // virtual destructor so deleting through an Order pointer is safe
    virtual ~Order() = default;

    // getters
    std::uint64_t getID() const;
    std::uint64_t getSequence() const;
    int getTraderID() const;
    const std::string& getSymbol() const;
    OrderSide getOrderSide() const;
    Quantity getQuantity() const;
    Price getPrice() const;

    bool isFilled() const;

    // reduces remaining shares after a (partial) fill; throws if invalid
    void reduceQuantity(Quantity amount);

    // "LIMIT" or "MARKET"
    virtual std::string getOrderType() const = 0;
};

// Executes only at `price` or better.
class LimitOrder : public Order {
public:
    LimitOrder(int trader_id, std::string symbol, OrderSide side,
               Quantity quantity, Price limit_price);
    std::string getOrderType() const override;
};

// Executes immediately at the best available price.
class MarketOrder : public Order {
public:
    MarketOrder(int trader_id, std::string symbol, OrderSide side,
                Quantity quantity);
    std::string getOrderType() const override;
};

#endif