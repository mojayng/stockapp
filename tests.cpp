// Unit tests for the order book and engine. No external framework needed.
//
// Build:  g++ -std=c++17 -Wall -Wextra -pthread tests.cpp Order.cpp OrderBook.cpp Engine.cpp -o tests
// Run:    ./tests        (exit code 0 = all passed)

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"
#include "order.h"
#include "orderbook.h"

// ---------------------------------------------------------------- mini test harness

namespace {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures_in_current_test = 0;

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* a_text,
                const char* b_text, const char* file, int line) {
    if (!(actual == expected)) {
        ++g_failures_in_current_test;
        std::ostringstream msg;
        msg << "    FAIL " << file << ":" << line << "  " << a_text << " == " << b_text
            << "\n         actual: " << actual << "   expected: " << expected;
        std::cout << msg.str() << "\n";
    }
}

}  // namespace

#define TEST(name)                                          \
    static void name();                                     \
    static Registrar registrar_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failures_in_current_test;                                    \
            std::cout << "    FAIL " << __FILE__ << ":" << __LINE__ << "  "  \
                      << #cond << "\n";                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected) \
    checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_THROWS(expr)                                                      \
    do {                                                                        \
        bool threw_ = false;                                                    \
        try { expr; } catch (const std::exception&) { threw_ = true; }          \
        if (!threw_) {                                                          \
            ++g_failures_in_current_test;                                       \
            std::cout << "    FAIL " << __FILE__ << ":" << __LINE__             \
                      << "  expected an exception from: " << #expr << "\n";     \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------- helpers

namespace {

using S = OrderSide;

std::unique_ptr<Order> limitOrder(int trader, S side, int qty, double dollars) {
    return std::make_unique<LimitOrder>(trader, "AAPL", side, qty, dollarsToCents(dollars));
}

std::unique_ptr<Order> marketOrder(int trader, S side, int qty) {
    return std::make_unique<MarketOrder>(trader, "AAPL", side, qty);
}

}  // namespace

// ---------------------------------------------------------------- Order tests

TEST(dollars_convert_to_exact_cents) {
    CHECK_EQ(dollarsToCents(187.35), 18735);
    CHECK_EQ(dollarsToCents(0.29), 29);   // 0.29 * 100 is 28.999... in floating point
    CHECK_EQ(dollarsToCents(100.00), 10000);
    CHECK_EQ(centsToDollars(18735), 187.35);
}

TEST(order_rejects_bad_input) {
    CHECK_THROWS(LimitOrder(1, "AAPL", S::Buy, 0, 100));    // zero quantity
    CHECK_THROWS(LimitOrder(1, "AAPL", S::Buy, -5, 100));   // negative quantity
    CHECK_THROWS(LimitOrder(1, "AAPL", S::Buy, 10, 0));     // zero limit price
    CHECK_THROWS(MarketOrder(1, "AAPL", S::Buy, 0));
}

TEST(order_fills_reduce_quantity_and_reject_overfill) {
    LimitOrder o(1, "AAPL", S::Buy, 100, 10000);
    o.reduceQuantity(40);
    CHECK_EQ(o.getQuantity(), 60);
    CHECK(!o.isFilled());
    CHECK_THROWS(o.reduceQuantity(61));  // more than remaining
    CHECK_THROWS(o.reduceQuantity(0));
    o.reduceQuantity(60);
    CHECK(o.isFilled());
}

TEST(orders_get_increasing_sequence_numbers) {
    LimitOrder a(1, "AAPL", S::Buy, 1, 100);
    LimitOrder b(1, "AAPL", S::Buy, 1, 100);
    CHECK(a.getSequence() < b.getSequence());
    CHECK(a.getID() != b.getID());
}

// ---------------------------------------------------------------- OrderBook tests

TEST(non_crossing_orders_rest_in_book) {
    OrderBook book("AAPL");
    CHECK(book.addOrder(limitOrder(1, S::Buy, 10, 99.00)).empty());
    CHECK(book.addOrder(limitOrder(2, S::Sell, 10, 101.00)).empty());
    CHECK_EQ(book.bestBid().value(), 9900);
    CHECK_EQ(book.bestAsk().value(), 10100);
    CHECK_EQ(book.restingOrderCount(), 2u);
}

TEST(empty_book_has_no_best_prices) {
    OrderBook book("AAPL");
    CHECK(!book.bestBid().has_value());
    CHECK(!book.bestAsk().has_value());
}

TEST(crossing_limit_orders_trade_at_resting_price) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 10, 100.00));
    // Buyer is willing to pay 105 but should get the resting 100.
    auto trades = book.addOrder(limitOrder(2, S::Buy, 10, 105.00));
    CHECK_EQ(trades.size(), 1u);
    CHECK_EQ(trades.at(0).price, 10000);
    CHECK_EQ(trades.at(0).quantity, 10);
    CHECK_EQ(trades.at(0).buyer_id, 2);
    CHECK_EQ(trades.at(0).seller_id, 1);
    CHECK_EQ(book.restingOrderCount(), 0u);
}

TEST(limit_at_exactly_the_opposite_price_trades) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 5, 100.00));
    CHECK_EQ(book.addOrder(limitOrder(2, S::Buy, 5, 100.00)).size(), 1u);
}

TEST(better_price_matches_first) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 10, 102.00));
    book.addOrder(limitOrder(2, S::Sell, 10, 101.00));  // better (lower) ask, arrived later
    auto trades = book.addOrder(limitOrder(3, S::Buy, 10, 105.00));
    CHECK_EQ(trades.size(), 1u);
    CHECK_EQ(trades.at(0).seller_id, 2);
    CHECK_EQ(trades.at(0).price, 10100);
}

TEST(sells_hit_the_highest_bid_first) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Buy, 10, 98.00));
    book.addOrder(limitOrder(2, S::Buy, 10, 99.00));
    auto trades = book.addOrder(limitOrder(3, S::Sell, 10, 97.00));
    CHECK_EQ(trades.size(), 1u);
    CHECK_EQ(trades.at(0).buyer_id, 2);
    CHECK_EQ(trades.at(0).price, 9900);
}

TEST(same_price_matches_in_arrival_order) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 50, 101.00));  // first in line
    book.addOrder(limitOrder(2, S::Sell, 50, 101.00));
    auto trades = book.addOrder(limitOrder(3, S::Buy, 70, 101.00));
    CHECK_EQ(trades.size(), 2u);
    CHECK_EQ(trades.at(0).seller_id, 1);
    CHECK_EQ(trades.at(0).quantity, 50);
    CHECK_EQ(trades.at(1).seller_id, 2);
    CHECK_EQ(trades.at(1).quantity, 20);
    // trader 2 keeps 30 resting at the front of the level
    CHECK_EQ(book.askLevels(5).at(0).second, 30);
}

TEST(partial_fill_leaves_remainder_resting) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 10, 100.00));
    auto trades = book.addOrder(limitOrder(2, S::Buy, 25, 100.00));
    CHECK_EQ(trades.size(), 1u);
    CHECK_EQ(trades.at(0).quantity, 10);
    // the unfilled 15 now rests as a bid
    CHECK_EQ(book.bestBid().value(), 10000);
    CHECK_EQ(book.bidLevels(5).at(0).second, 15);
    CHECK(!book.bestAsk().has_value());
}

TEST(market_order_sweeps_multiple_levels_and_drops_remainder) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Sell, 30, 101.00));
    book.addOrder(limitOrder(2, S::Sell, 100, 102.00));
    auto trades = book.addOrder(marketOrder(3, S::Buy, 150));
    CHECK_EQ(trades.size(), 2u);
    CHECK_EQ(trades.at(0).price, 10100);
    CHECK_EQ(trades.at(0).quantity, 30);
    CHECK_EQ(trades.at(1).price, 10200);
    CHECK_EQ(trades.at(1).quantity, 100);
    CHECK(!book.bestAsk().has_value());
    CHECK(!book.bestBid().has_value());  // the unfilled 20 must NOT rest
    CHECK_EQ(book.restingOrderCount(), 0u);
}

TEST(market_order_into_empty_book_does_nothing) {
    OrderBook book("AAPL");
    CHECK(book.addOrder(marketOrder(1, S::Buy, 10)).empty());
    CHECK_EQ(book.restingOrderCount(), 0u);
}

TEST(cancel_removes_order_and_empty_level) {
    OrderBook book("AAPL");
    auto high = limitOrder(1, S::Buy, 10, 100.00);
    auto low = limitOrder(2, S::Buy, 10, 99.00);
    const auto high_id = high->getID();
    book.addOrder(std::move(high));
    book.addOrder(std::move(low));
    CHECK_EQ(book.bestBid().value(), 10000);

    CHECK(book.cancelOrder(high_id));
    CHECK_EQ(book.bestBid().value(), 9900);          // the 100.00 level is gone
    CHECK(!book.cancelOrder(high_id));        // already cancelled
    CHECK(!book.cancelOrder(999999));         // never existed
}

TEST(cancelled_order_cannot_be_matched) {
    OrderBook book("AAPL");
    auto ask = limitOrder(1, S::Sell, 10, 100.00);
    const auto id = ask->getID();
    book.addOrder(std::move(ask));
    book.cancelOrder(id);
    CHECK(book.addOrder(limitOrder(2, S::Buy, 10, 100.00)).empty());
}

TEST(filled_order_cannot_be_cancelled) {
    OrderBook book("AAPL");
    auto ask = limitOrder(1, S::Sell, 10, 100.00);
    const auto id = ask->getID();
    book.addOrder(std::move(ask));
    book.addOrder(limitOrder(2, S::Buy, 10, 100.00));  // fully fills it
    CHECK(!book.cancelOrder(id));
}

TEST(wrong_symbol_is_rejected) {
    OrderBook book("AAPL");
    CHECK_THROWS(book.addOrder(std::make_unique<LimitOrder>(1, "MSFT", S::Buy, 1, 100)));
    CHECK_THROWS(book.addOrder(nullptr));
}

TEST(level_depth_aggregates_quantity) {
    OrderBook book("AAPL");
    book.addOrder(limitOrder(1, S::Buy, 10, 100.00));
    book.addOrder(limitOrder(2, S::Buy, 15, 100.00));
    book.addOrder(limitOrder(3, S::Buy, 5, 99.00));
    auto levels = book.bidLevels(10);
    CHECK_EQ(levels.size(), 2u);
    CHECK_EQ(levels.at(0).first, 10000);
    CHECK_EQ(levels.at(0).second, 25);
    CHECK_EQ(levels.at(1).first, 9900);
    CHECK_EQ(book.bidLevels(1).size(), 1u);  // depth limit respected
}

// ---------------------------------------------------------------- Engine tests (threaded)

TEST(engine_matches_orders_and_tracks_positions) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.start();
    engine.submitLimit(2, S::Sell, 10, dollarsToCents(100.00));
    engine.submitMarket(1, S::Buy, 10);
    engine.stop();  // drains the queue and publishes before returning

    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.total_trades, 1);
    CHECK_EQ(s.total_volume, 10);
    CHECK_EQ(s.last_price, 10000);
    CHECK_EQ(s.positions.at(1).shares, 10);
    CHECK_EQ(s.positions.at(1).cash, -100000);   // paid $1000.00
    CHECK_EQ(s.positions.at(2).shares, -10);
    CHECK_EQ(s.positions.at(2).cash, 100000);
    CHECK_EQ(s.pnl(1), 0);                       // marked at the price paid
}

TEST(engine_pnl_follows_last_price) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.start();
    engine.submitLimit(2, S::Sell, 10, dollarsToCents(100.00));
    engine.submitMarket(1, S::Buy, 10);              // trader 1 buys 10 @ 100
    engine.submitLimit(3, S::Buy, 1, dollarsToCents(102.00));
    engine.submitMarket(4, S::Sell, 1);              // price prints at 102
    engine.stop();

    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.last_price, 10200);
    CHECK_EQ(s.pnl(1), 2000);                        // 10 shares * $2.00 gain, in cents
}

TEST(engine_rejects_invalid_requests_on_the_calling_thread) {
    Engine engine("AAPL", dollarsToCents(100.00));
    CHECK_THROWS(engine.submitLimit(1, S::Buy, 0, 10000));
    CHECK_THROWS(engine.submitLimit(1, S::Buy, 10, 0));
    CHECK_THROWS(engine.submitMarket(1, S::Buy, -1));
}

TEST(engine_snapshot_before_any_trade_shows_start_price) {
    Engine engine("AAPL", dollarsToCents(100.00));
    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.last_price, 10000);
    CHECK_EQ(s.total_trades, 0);
}

TEST(engine_records_timestamped_price_history) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.start();
    engine.submitLimit(2, S::Sell, 5, dollarsToCents(100.50));
    engine.submitMarket(1, S::Buy, 5);
    engine.submitLimit(3, S::Sell, 5, dollarsToCents(100.75));
    engine.submitMarket(1, S::Buy, 5);
    engine.stop();

    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.price_history.size(), 3u);  // opening point + 2 trades
    CHECK_EQ(s.price_history.front().price, 10000);
    CHECK_EQ(s.price_history.back().price, 10075);
    for (std::size_t i = 1; i < s.price_history.size(); ++i) {
        CHECK(s.price_history[i].time_s >= s.price_history[i - 1].time_s);  // time never goes backwards
    }
    CHECK(engine.elapsedSeconds() >= s.price_history.back().time_s);
}

TEST(engine_records_fills_for_the_tracked_trader_only) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.trackFills(1);
    engine.start();
    engine.submitLimit(2, S::Sell, 10, dollarsToCents(100.00));
    engine.submitMarket(1, S::Buy, 10);                           // you buy 10 -> position +10
    engine.submitLimit(3, S::Buy, 4, dollarsToCents(101.00));
    engine.submitMarket(1, S::Sell, 4);                           // you sell 4 -> position +6
    engine.submitLimit(5, S::Sell, 3, dollarsToCents(102.00));
    engine.submitMarket(4, S::Buy, 3);                            // someone else trades: not yours
    engine.stop();

    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.my_fills.size(), 2u);
    CHECK(s.my_fills.at(0).side == S::Buy);
    CHECK_EQ(s.my_fills.at(0).quantity, 10);
    CHECK_EQ(s.my_fills.at(0).price, 10000);
    CHECK_EQ(s.my_fills.at(0).position_after, 10);
    CHECK(s.my_fills.at(1).side == S::Sell);
    CHECK_EQ(s.my_fills.at(1).quantity, 4);
    CHECK_EQ(s.my_fills.at(1).price, 10100);
    CHECK_EQ(s.my_fills.at(1).position_after, 6);
    CHECK(s.my_fills.at(1).time_s >= s.my_fills.at(0).time_s);
}

TEST(engine_records_no_fills_when_nobody_is_tracked) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.start();
    engine.submitLimit(2, S::Sell, 10, dollarsToCents(100.00));
    engine.submitMarket(1, S::Buy, 10);
    engine.stop();
    CHECK(engine.snapshot().my_fills.empty());
}

TEST(engine_handles_many_threads_submitting_at_once) {
    Engine engine("AAPL", dollarsToCents(100.00));
    engine.start();

    // 4 threads each post 500 one-share sell orders at $100.00.
    std::vector<std::thread> sellers;
    for (int t = 0; t < 4; ++t) {
        sellers.emplace_back([&engine, t] {
            for (int i = 0; i < 500; ++i) {
                engine.submitLimit(10 + t, S::Sell, 1, dollarsToCents(100.00));
            }
        });
    }
    for (auto& th : sellers) th.join();

    // Everything above is queued before this buy, so it should take all 2000 shares.
    engine.submitMarket(1, S::Buy, 2000);
    engine.stop();

    const MarketSnapshot s = engine.snapshot();
    CHECK_EQ(s.total_trades, 2000);
    CHECK_EQ(s.total_volume, 2000);
    CHECK_EQ(s.positions.at(1).shares, 2000);
    CHECK(s.asks.empty());
}

// ---------------------------------------------------------------- runner

int main() {
    int failed_tests = 0;
    for (const TestCase& test : registry()) {
        g_failures_in_current_test = 0;
        try {
            test.fn();
        } catch (const std::exception& e) {
            ++g_failures_in_current_test;
            std::cout << "    UNEXPECTED EXCEPTION: " << e.what() << "\n";
        }
        if (g_failures_in_current_test == 0) {
            std::cout << "[ PASS ] " << test.name << "\n";
        } else {
            std::cout << "[ FAIL ] " << test.name << "\n";
            ++failed_tests;
        }
    }
    const int total = static_cast<int>(registry().size());
    std::cout << "\n" << (total - failed_tests) << "/" << total << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}
