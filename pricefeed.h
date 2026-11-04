#ifndef PRICEFEED_H
#define PRICEFEED_H

#include <atomic>
#include <chrono>
#include <thread>

#include "engine.h"
#include "order.h"

// Simulated market: a background "bot" thread random-walks a fair price and
// keeps the book alive by posting limit orders around it, plus an occasional
// market order. Its orders go through the same queue as yours.
class PriceFeed {
public:
    PriceFeed(Engine& engine, Price start_price, int bot_trader_id = 0,
              std::chrono::milliseconds interval = std::chrono::milliseconds(100));
    ~PriceFeed();
    PriceFeed(const PriceFeed&) = delete;
    PriceFeed& operator=(const PriceFeed&) = delete;

    void start();
    void stop();

private:
    void run();

    Engine& engine_;
    Price fair_price_;
    int bot_id_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

#endif
