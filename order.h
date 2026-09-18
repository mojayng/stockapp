#ifndef ORDER_H
#define ORDER_H

#include <string>
#include  "stock.h"
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
    time_t timestamp;
    OrderType type;
    public:
    Order(const stock& s, OrderType t) : price(s.getPrice()), type(t) {}
    string getOrdertype() const;

};



#endif