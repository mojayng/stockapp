#include "engine.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::size_t kMaxRecentTrades = 50;
constexpr std::size_t kMaxPriceHistory = 5000;
constexpr int kMaxBatch = 256;  // publish at least this often under heavy load
}  // namespace

long long MarketSnapshot::pnl(int trader_id) const {
    auto it = positions.find(trader_id);
    if (it == positions.end()) return 0;
    return it->second.cash + it->second.shares * last_price;
}

Engine::Engine(std::string symbol, Price start_price)
    : start_time_(std::chrono::steady_clock::now()),
      book_(symbol),
      last_price_(start_price) {
    price_history_.push_back(PricePoint{0.0, start_price});  // chart starts at the opening price
    snap_.symbol = std::move(symbol);
    snap_.last_price = start_price;
}

Engine::~Engine() { stop(); }

void Engine::start() {
    if (running_.exchange(true)) return;  // already running
    worker_ = std::thread(&Engine::run, this);
}

void Engine::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void Engine::submitLimit(int trader_id, OrderSide side, Quantity quantity, Price price) {
    if (quantity <= 0) throw std::invalid_argument("Quantity must be positive");
    if (price <= 0) throw std::invalid_argument("Limit price must be positive");
    Request req;
    req.is_market = false;
    req.trader_id = trader_id;
    req.side = side;
    req.quantity = quantity;
    req.price = price;
    queue_.push(req);
}

void Engine::submitMarket(int trader_id, OrderSide side, Quantity quantity) {
    if (quantity <= 0) throw std::invalid_argument("Quantity must be positive");
    Request req;
    req.is_market = true;
    req.trader_id = trader_id;
    req.side = side;
    req.quantity = quantity;
    queue_.push(req);
}

double Engine::elapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
}

MarketSnapshot Engine::snapshot() const {
    std::lock_guard<std::mutex> lock(snap_mutex_);
    return snap_;
}

void Engine::run() {
    using namespace std::chrono_literals;
    Request req;
    while (running_) {
        if (!queue_.waitPopFor(req, 50ms)) continue;  // timeout: re-check running_
        int handled = 0;
        do {
            handle(req);
            ++handled;
        } while (handled < kMaxBatch && queue_.tryPop(req));
        publish();
    }
    // Shutting down: finish whatever is still queued.
    bool any = false;
    while (queue_.tryPop(req)) {
        handle(req);
        any = true;
    }
    if (any) publish();
}

void Engine::handle(const Request& req) {
    try {
        const double now_s = elapsedSeconds();
        std::unique_ptr<Order> order;
        if (req.is_market) {
            order = std::make_unique<MarketOrder>(req.trader_id, book_.getSymbol(),
                                                  req.side, req.quantity);
        } else {
            order = std::make_unique<LimitOrder>(req.trader_id, book_.getSymbol(),
                                                 req.side, req.quantity, req.price);
        }

        for (const Trade& t : book_.addOrder(std::move(order))) {
            last_price_ = t.price;
            total_trades_ += 1;
            total_volume_ += t.quantity;

            recent_trades_.push_back(t);
            if (recent_trades_.size() > kMaxRecentTrades) recent_trades_.pop_front();
            price_history_.push_back(PricePoint{now_s, t.price});
            if (price_history_.size() > kMaxPriceHistory) price_history_.pop_front();

            const long long value = static_cast<long long>(t.price) * t.quantity;
            Position& buyer = positions_[t.buyer_id];
            buyer.shares += t.quantity;
            buyer.cash -= value;
            Position& seller = positions_[t.seller_id];
            seller.shares -= t.quantity;
            seller.cash += value;
        }
    } catch (const std::exception& e) {
        // Never let one bad request kill the engine thread.
        std::cerr << "Engine: dropped request: " << e.what() << "\n";
    }
}

void Engine::publish() {
    MarketSnapshot s;
    s.symbol = book_.getSymbol();
    s.last_price = last_price_;
    s.best_bid = book_.bestBid().value_or(0);
    s.best_ask = book_.bestAsk().value_or(0);
    s.bids = book_.bidLevels(10);
    s.asks = book_.askLevels(10);
    s.recent_trades.assign(recent_trades_.begin(), recent_trades_.end());
    s.price_history.assign(price_history_.begin(), price_history_.end());
    s.positions = positions_;
    s.total_trades = total_trades_;
    s.total_volume = total_volume_;

    std::lock_guard<std::mutex> lock(snap_mutex_);
    snap_ = std::move(s);
}