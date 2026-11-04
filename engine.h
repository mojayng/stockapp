#ifndef ENGINE_H
#define ENGINE_H

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "order.h"
#include "orderbook.h"
#include "threadsafequeue.h"

// A trader's holdings. cash is in cents (negative after buying).
struct Position {
    long long shares = 0;
    long long cash = 0;
};

// A read-only copy of the market at one moment. Other threads (console, GUI)
// read this; they never touch the order book itself.
struct MarketSnapshot {
    std::string symbol;
    Price last_price = 0;
    Price best_bid = 0;  // 0 means "no bids"
    Price best_ask = 0;  // 0 means "no asks"
    std::vector<std::pair<Price, Quantity>> bids;  // best first
    std::vector<std::pair<Price, Quantity>> asks;  // best first
    std::vector<Trade> recent_trades;              // oldest first
    std::vector<Price> price_history;              // last-trade prices, oldest first
    std::unordered_map<int, Position> positions;   // by trader id
    long long total_trades = 0;
    long long total_volume = 0;

    // Profit/loss in cents, marking the position at the last traded price.
    long long pnl(int trader_id) const;
};

// Owns the order book and runs it on ONE dedicated thread.
//
//   any thread --submitX()--> [queue] --> engine thread --> OrderBook
//                                              |
//                                         publishes MarketSnapshot
//   any thread <------------snapshot()---------'
//
// Because only the engine thread touches the book, the book needs no locks.
class Engine {
public:
    Engine(std::string symbol, Price start_price);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    void stop();  // finishes queued requests, then joins the thread

    // Thread-safe. Throw std::invalid_argument for a bad quantity or price.
    void submitLimit(int trader_id, OrderSide side, Quantity quantity, Price price);
    void submitMarket(int trader_id, OrderSide side, Quantity quantity);

    // Thread-safe. Returns a copy of the latest published state.
    MarketSnapshot snapshot() const;

private:
    struct Request {
        bool is_market = false;
        int trader_id = 0;
        OrderSide side = OrderSide::Buy;
        Quantity quantity = 0;
        Price price = 0;
    };

    void run();
    void handle(const Request& req);
    void publish();

    ThreadSafeQueue<Request> queue_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    // ---- touched only by the engine thread ----
    OrderBook book_;
    Price last_price_;
    std::deque<Trade> recent_trades_;
    std::deque<Price> price_history_;
    std::unordered_map<int, Position> positions_;
    long long total_trades_ = 0;
    long long total_volume_ = 0;

    // ---- shared with readers, guarded by the mutex ----
    mutable std::mutex snap_mutex_;
    MarketSnapshot snap_;
};

#endif
