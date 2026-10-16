#include "stock.h"

Stock::Stock(std::string name, double price)
    : name(name), price(price) {}

void  Stock :: setName(std::string name){
    this-> name = name;
}

void Stock :: setPrice(double price ){
    this -> price = price;
}

std::string Stock::getName()const{
    return name;
}

double Stock:: getPrice()const{
    return price;
}