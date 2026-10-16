#ifndef ORDER_H
#define ORDER_H

#include <string> 
#include <ctime>

// integer type that can only be buy(0) or sell(1)
enum OrderType{
    Buy,
    Sell
};

class Order{
    private: // private so outside code can't modify directly
    int trader_id;
    int quantity;
    double price;
    std::time_t timestamp; // numberr of seconds
    OrderType type;

    public:
    // constructor and destructor
    // destructes when order is destroyed
    // virtual deletes a market order, not an Order pointer
    Order(int trader_id, double price, int quantity, OrderType type);
    virtual ~Order() = default;

    // getters
    int getTraderID() const;
    double getPrice() const;
    int getQuantity() const;
    std::time_t getTimestamp() const;
    OrderType getOrderSide() const;  // returns Buy/Sell

    // reduces shares until order is filled
    void reduceQuantity(int amount);
    void setOrderType(OrderType newType);

    virtual std::string getOrderType() const = 0;
    

};



#endif