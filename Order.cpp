#include "order.h"
#include <ctime>
#include <stdexcept>

Order::Order(int trader_id, double price, int quantity, OrderType type)
    : trader_id(trader_id), quantity(quantity), price(price),
      timestamp(std::time(nullptr)), type(type) {}

int Order::getTraderID() const{
    return trader_id;
}

double Order::getPrice() const{
    return price;
}
int Order::getQuantity() const{
    return quantity;
}
std::time_t Order::getTimestamp() const{
    return timestamp;
}
OrderType Order::getOrderSide() const{
    return type;
}
void Order::setOrderType(OrderType newType){
    type = newType;
}
void Order::reduceQuantity(int amount){
    if(amount <=0 || amount > quantity){
        throw std::invalid_argument("Invalid fill amount");
    }
    quantity -= amount;
}