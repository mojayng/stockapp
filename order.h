#ifndef ORDER_H
#define ORDER_H

#include <string>
#include "stock.h"
#include <ctime>

enum OrderType{
    Buy,
    Sell
};

class Order{
    private:
    int trader_id;
    int quantity;
    double price;
    std::time_t timestamp;
    OrderType type;

    public:
    Order(int trader_id, double price, int quantity, OrderType type);
    int getTraiderID() const;
    double getPrice() const;
    int getQuantity() const;
    time_t getTimestamp() const;
    OrderType getOrdertype() const;


    void reduceQuantity(int amount);


    void setOrderType(OrderType type);
    

};



#endif