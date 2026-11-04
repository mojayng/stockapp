#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include "engine.h"
#include "order.h"
#include "pricefeed.h"

namespace {
double usd(Price cents) { return centsToDollars(cents); }
}  // namespace

int main() {
    using namespace std::chrono_literals;

    const Price start = dollarsToCents(100.00);
    const int me = 1;  // your trader id; the bot is trader 0

    Engine engine("AAPL", start);
    PriceFeed feed(engine, start);
    engine.start();
    feed.start();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Live simulation: AAPL starting at $" << usd(start) << "\n\n";

    for (int tick = 1; tick <= 12; ++tick) {  // 12 x 500 ms = 6 seconds
        std::this_thread::sleep_for(500ms);

        if (tick == 4) {
            std::cout << "  >> you: MARKET BUY 50\n";
            engine.submitMarket(me, OrderSide::Buy, 50);
        }
        if (tick == 9) {
            std::cout << "  >> you: MARKET SELL 50\n";
            engine.submitMarket(me, OrderSide::Sell, 50);
        }

        const MarketSnapshot s = engine.snapshot();
        std::cout << "t=" << std::setw(4) << (tick * 0.5) << "s"
                  << "  last $" << usd(s.last_price)
                  << "  bid $" << usd(s.best_bid)
                  << "  ask $" << usd(s.best_ask)
                  << "  trades " << s.total_trades
                  << "  volume " << s.total_volume << "\n";
    }

    // Stop producers first, then the engine, so nothing is left in the queue.
    feed.stop();
    engine.stop();

    const MarketSnapshot s = engine.snapshot();
    std::cout << "\n=== Final book (" << s.symbol << ") ===\n";
    for (auto it = s.asks.rbegin(); it != s.asks.rend(); ++it) {
        std::cout << "  ASK  " << std::setw(8) << usd(it->first) << "  x " << it->second << "\n";
    }
    std::cout << "  ------------------------\n";
    for (const auto& [price, qty] : s.bids) {
        std::cout << "  BID  " << std::setw(8) << usd(price) << "  x " << qty << "\n";
    }

    const auto pos = s.positions.find(me);
    const long long shares = pos == s.positions.end() ? 0 : pos->second.shares;
    std::cout << "\nYour position: " << shares << " shares\n"
              << "Your P&L: $" << usd(s.pnl(me)) << " (marked at last price $"
              << usd(s.last_price) << ")\n";
    return 0;
}
