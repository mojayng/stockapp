#ifndef STOCK_H
#define STOCK_H

#include <string>

class Stock{
    private
    std::string name;
    double price;

public:
        //construct
        Stock(std::string name, double price);

        //setters
        void setName(std::string name);
        void setPrice(double price);

        // getters
        std::string getName() const;
        double getPrice() const;
};

#endif