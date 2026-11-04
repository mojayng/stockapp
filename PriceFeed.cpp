#include "pricefeed.h"

#include <algorithm>
#include <random>

PriceFeed::PriceFeed(Engine& engine, Price start_price, int bot_trader_id,
                     std::chrono::milliseconds interval)
    : engine_(engine),
      fair_price_(start_price),
      bot_id_(bot_trader_id),
      interval_(interval) {}

PriceFeed::~PriceFeed() { stop(); }

void PriceFeed::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&PriceFeed::run, this);
}

void PriceFeed::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void PriceFeed::run() {
    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<double> step_cents(0.0, 8.0);   // random-walk step
    std::uniform_int_distribution<int> offset_cents(1, 10);  // distance from fair price
    std::uniform_int_distribution<int> limit_qty(10, 100);
    std::uniform_int_distribution<int> market_qty(20, 60);
    std::bernoulli_distribution coin(0.5);

    const Price min_price = 100;  // never let the price fall below $1.00
    long long tick = 0;

    while (running_) {
        // 1. Move the fair price.
        fair_price_ += static_cast<Price>(step_cents(rng));
        fair_price_ = std::max(fair_price_, min_price);

        // 2. Post a bid below and an ask above the fair price.
        const Price bid = std::max<Price>(1, fair_price_ - offset_cents(rng));
        const Price ask = fair_price_ + offset_cents(rng);
        engine_.submitLimit(bot_id_, OrderSide::Buy, limit_qty(rng), bid);
        engine_.submitLimit(bot_id_, OrderSide::Sell, limit_qty(rng), ask);

        // 3. Every third tick, a market order consumes some liquidity.
        if (tick % 3 == 0) {
            engine_.submitMarket(bot_id_, coin(rng) ? OrderSide::Buy : OrderSide::Sell,
                                 market_qty(rng));
        }

        ++tick;
        std::this_thread::sleep_for(interval_);
    }
}
