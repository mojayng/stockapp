// Simulation window for the stock trading engine.
//
// This file only DISPLAYS the market and forwards your clicks. All matching,
// price movement and P&L bookkeeping happens in the engine (Engine, OrderBook,
// PriceFeed). Each frame the window copies the latest MarketSnapshot, draws it,
// and any button press calls engine.submitMarket()/submitLimit().

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "engine.h"
#include "order.h"
#include "pricefeed.h"

namespace {

constexpr int kMe = 1;  // your trader id; the bot is trader 0

const ImVec4 kGreen(0.30f, 0.85f, 0.45f, 1.0f);
const ImVec4 kRed(0.95f, 0.35f, 0.35f, 1.0f);
const ImVec4 kGrey(0.65f, 0.65f, 0.65f, 1.0f);

double usd(long long cents) { return static_cast<double>(cents) / 100.0; }

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

// A coloured button: pushes three colours, draws, pops them.
bool colouredButton(const char* label, const ImVec4& base, const ImVec2& size) {
    const ImVec4 hover(base.x * 1.2f, base.y * 1.2f, base.z * 1.2f, 1.0f);
    const ImVec4 active(base.x * 0.8f, base.y * 0.8f, base.z * 0.8f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

// ---------------------------------------------------------------- panels

// One candlestick: the open/high/low/close of all trades in one time bucket.
struct Candle {
    double x;  // centre of the bucket, in seconds
    double open;
    double high;
    double low;
    double close;
};

// Groups the trade history into fixed-size time buckets (e.g. 2 seconds each).
std::vector<Candle> buildCandles(const std::vector<PricePoint>& history, double interval) {
    std::vector<Candle> out;
    long long current_bucket = -1;
    for (const PricePoint& p : history) {
        const long long bucket = static_cast<long long>(p.time_s / interval);
        const double price = usd(p.price);
        if (out.empty() || bucket != current_bucket) {
            out.push_back(Candle{(static_cast<double>(bucket) + 0.5) * interval, price, price,
                                 price, price});
            current_bucket = bucket;
        } else {
            Candle& c = out.back();
            c.high = std::max(c.high, price);
            c.low = std::min(c.low, price);
            c.close = price;
        }
    }
    return out;
}

// Draws candles with the plot's own draw list. Green = price rose over the
// bucket (close >= open), red = price fell. Must be called inside BeginPlot().
void drawCandles(const std::vector<Candle>& candles, double interval) {
    const ImU32 green = IM_COL32(77, 217, 115, 255);
    const ImU32 red = IM_COL32(242, 89, 89, 255);
    const double half_width = interval * 0.35;

    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();  // don't draw outside the plot area
    for (const Candle& c : candles) {
        const ImU32 colour = c.close >= c.open ? green : red;
        const ImVec2 wick_low = ImPlot::PlotToPixels(c.x, c.low);
        const ImVec2 wick_high = ImPlot::PlotToPixels(c.x, c.high);
        const ImVec2 body_tl = ImPlot::PlotToPixels(c.x - half_width, std::max(c.open, c.close));
        ImVec2 body_br = ImPlot::PlotToPixels(c.x + half_width, std::min(c.open, c.close));
        if (body_br.y - body_tl.y < 1.0f) body_br.y = body_tl.y + 1.0f;  // flat candle: still visible
        draw_list->AddLine(wick_low, wick_high, colour, 1.0f);
        draw_list->AddRectFilled(body_tl, body_br, colour);
    }
    ImPlot::PopPlotClipRect();
}

void drawPricePanel(const MarketSnapshot& s, double now_s) {
    static float window_seconds = 60.0f;  // how much history the live view shows
    static bool paused = false;           // true = the view stops following; pan and zoom freely
    static int chart_type = 0;            // 0 = candles, 1 = line
    static int interval_index = 1;        // candle size: 0 = 1s, 1 = 2s, 2 = 5s, 3 = 10s
    static bool show_bid_ask = true;
    static const double kIntervals[] = {1.0, 2.0, 5.0, 10.0};
    const double interval = kIntervals[interval_index];

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Price");

    ImGui::Text("%s   last $%.2f   t = %.1fs", s.symbol.c_str(), usd(s.last_price), now_s);
    ImGui::SameLine();
    if (paused) ImGui::TextColored(kRed, "   PAUSED (drag to pan, wheel to zoom, double-click to fit)");
    else ImGui::TextColored(kGreen, "   LIVE");

    if (ImGui::Button(paused ? "Resume live" : "Pause chart")) paused = !paused;
    ImGui::SameLine();
    ImGui::RadioButton("Candles", &chart_type, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Line", &chart_type, 1);
    if (chart_type == 0) {
        ImGui::SameLine();
        ImGui::Text(" Candle size:");
        ImGui::SameLine();
        ImGui::RadioButton("1s", &interval_index, 0);
        ImGui::SameLine();
        ImGui::RadioButton("2s", &interval_index, 1);
        ImGui::SameLine();
        ImGui::RadioButton("5s", &interval_index, 2);
        ImGui::SameLine();
        ImGui::RadioButton("10s", &interval_index, 3);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show bid/ask", &show_bid_ask);
    ImGui::SliderFloat("Live window (s)", &window_seconds, 10.0f, 300.0f, "%.0f");

    // Live view: right edge = now. The left edge never goes below 0 seconds.
    const double x_max = std::max(now_s, static_cast<double>(window_seconds));
    const double x_min = x_max - static_cast<double>(window_seconds);

    if (ImPlot::BeginPlot("##price", ImVec2(-1, -1))) {
        // While live, the Y axis auto-fits to what's visible. While paused it is left free.
        const ImPlotAxisFlags y_flags =
            paused ? ImPlotAxisFlags_None : (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
        ImPlot::SetupAxes("Time (s)", "Price ($)", ImPlotAxisFlags_None, y_flags);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0.0, 1.0e9);  // no negative time
        if (!paused) {
            ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImGuiCond_Always);
        }

        if (chart_type == 0) {
            const std::vector<Candle> candles = buildCandles(s.price_history, interval);
            // Invisible high/low lines: they exist only so the Y axis can auto-fit to the candles.
            std::vector<double> cx;
            std::vector<double> chigh;
            std::vector<double> clow;
            for (const Candle& c : candles) {
                cx.push_back(c.x);
                chigh.push_back(c.high);
                clow.push_back(c.low);
            }
            const int n = static_cast<int>(cx.size());
            ImPlot::SetNextLineStyle(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImPlot::PlotLine("##high", cx.data(), chigh.data(), n);
            ImPlot::SetNextLineStyle(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImPlot::PlotLine("##low", cx.data(), clow.data(), n);
            drawCandles(candles, interval);
        } else {
            std::vector<double> xs;
            std::vector<double> ys;
            xs.reserve(s.price_history.size() + 1);
            ys.reserve(s.price_history.size() + 1);
            for (const PricePoint& p : s.price_history) {
                xs.push_back(p.time_s);
                ys.push_back(usd(p.price));
            }
            // Extend the line flat to "now": between trades the price hasn't changed.
            xs.push_back(now_s);
            ys.push_back(usd(s.last_price));
            ImPlot::PlotLine(s.symbol.c_str(), xs.data(), ys.data(), static_cast<int>(xs.size()));
        }

        if (show_bid_ask) {
            const double xs2[2] = {0.0, std::max(now_s, 1.0)};
            if (s.best_bid > 0) {
                const double y[2] = {usd(s.best_bid), usd(s.best_bid)};
                ImPlot::SetNextLineStyle(kGreen, 1.5f);
                ImPlot::PlotLine("Best bid", xs2, y, 2);
            }
            if (s.best_ask > 0) {
                const double y[2] = {usd(s.best_ask), usd(s.best_ask)};
                ImPlot::SetNextLineStyle(kRed, 1.5f);
                ImPlot::PlotLine("Best ask", xs2, y, 2);
            }
        }

        // Grabbing or zooming the chart while live switches to paused, so your
        // own panning isn't overridden on the next frame.
        if (!paused && ImPlot::IsPlotHovered() &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::GetIO().MouseWheel != 0.0f)) {
            paused = true;
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void drawBookPanel(const MarketSnapshot& s) {
    ImGui::SetNextWindowPos(ImVec2(820, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Order Book");

    if (s.best_bid > 0 && s.best_ask > 0) {
        ImGui::Text("Spread: $%.2f", usd(s.best_ask - s.best_bid));
    } else {
        ImGui::TextDisabled("Spread: n/a");
    }
    ImGui::Separator();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders;

    ImGui::TextColored(kRed, "Asks (sellers)");
    if (ImGui::BeginTable("asks", 2, flags)) {
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Shares");
        ImGui::TableHeadersRow();
        // Highest ask at the top so the best ask sits right above the best bid.
        for (auto it = s.asks.rbegin(); it != s.asks.rend(); ++it) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kRed, "%.2f", usd(it->first));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", it->second);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextColored(kGreen, "Bids (buyers)");
    if (ImGui::BeginTable("bids", 2, flags)) {
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Shares");
        ImGui::TableHeadersRow();
        for (const auto& level : s.bids) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kGreen, "%.2f", usd(level.first));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", level.second);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void drawTradePanel(Engine& engine, PriceFeed& feed, const MarketSnapshot& s,
                    bool& bot_running) {
    static int quantity = 10;
    static double limit_price = 0.0;
    static std::string status = "Ready.";

    // Start the limit price box at the current price the first time through.
    if (limit_price <= 0.0 && s.last_price > 0) limit_price = usd(s.last_price);

    ImGui::SetNextWindowPos(ImVec2(10, 490), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Trade");

    ImGui::InputInt("Shares", &quantity);
    quantity = std::clamp(quantity, 1, 100000);

    const ImVec2 button_size(190, 32);
    if (colouredButton("Market BUY", ImVec4(0.12f, 0.50f, 0.25f, 1.0f), button_size)) {
        try {
            engine.submitMarket(kMe, OrderSide::Buy, quantity);
            status = "Sent: MARKET BUY " + std::to_string(quantity);
        } catch (const std::exception& e) {
            status = std::string("Rejected: ") + e.what();
        }
    }
    ImGui::SameLine();
    if (colouredButton("Market SELL", ImVec4(0.65f, 0.18f, 0.18f, 1.0f), button_size)) {
        try {
            engine.submitMarket(kMe, OrderSide::Sell, quantity);
            status = "Sent: MARKET SELL " + std::to_string(quantity);
        } catch (const std::exception& e) {
            status = std::string("Rejected: ") + e.what();
        }
    }

    ImGui::Spacing();
    ImGui::InputDouble("Limit price ($)", &limit_price, 0.01, 0.10, "%.2f");
    if (colouredButton("Limit BUY", ImVec4(0.12f, 0.50f, 0.25f, 1.0f), button_size)) {
        try {
            engine.submitLimit(kMe, OrderSide::Buy, quantity, dollarsToCents(limit_price));
            status = "Sent: LIMIT BUY " + std::to_string(quantity);
        } catch (const std::exception& e) {
            status = std::string("Rejected: ") + e.what();
        }
    }
    ImGui::SameLine();
    if (colouredButton("Limit SELL", ImVec4(0.65f, 0.18f, 0.18f, 1.0f), button_size)) {
        try {
            engine.submitLimit(kMe, OrderSide::Sell, quantity, dollarsToCents(limit_price));
            status = "Sent: LIMIT SELL " + std::to_string(quantity);
        } catch (const std::exception& e) {
            status = std::string("Rejected: ") + e.what();
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(kGrey, "%s", status.c_str());
    ImGui::TextColored(kGrey, "Unfilled limit orders stay in the book (no cancel yet).");
    ImGui::Separator();

    bool want_bot = bot_running;
    if (ImGui::Checkbox("Price bot running", &want_bot) && want_bot != bot_running) {
        if (want_bot) feed.start(); else feed.stop();
        bot_running = want_bot;
    }

    // Account summary.
    ImGui::Separator();
    long long shares = 0;
    long long cash = 0;
    const auto pos = s.positions.find(kMe);
    if (pos != s.positions.end()) {
        shares = pos->second.shares;
        cash = pos->second.cash;
    }
    const long long pnl = s.pnl(kMe);
    ImGui::Text("Position: %lld shares", shares);
    ImGui::Text("Cash flow: $%.2f", usd(cash));
    ImGui::Text("P&L: ");
    ImGui::SameLine();
    ImGui::TextColored(pnl >= 0 ? kGreen : kRed, "%s$%.2f", pnl < 0 ? "-" : "",
                       usd(pnl < 0 ? -pnl : pnl));

    ImGui::End();
}

void drawTradesPanel(const MarketSnapshot& s) {
    ImGui::SetNextWindowPos(ImVec2(440, 490), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(830, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Recent Trades");

    ImGui::Text("Total trades: %lld    Volume: %lld shares", s.total_trades, s.total_volume);

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("trades", 4, flags)) {
        ImGui::TableSetupColumn("Price");
        ImGui::TableSetupColumn("Shares");
        ImGui::TableSetupColumn("Buyer");
        ImGui::TableSetupColumn("Seller");
        ImGui::TableHeadersRow();
        int shown = 0;
        for (auto it = s.recent_trades.rbegin();
             it != s.recent_trades.rend() && shown < 25; ++it, ++shown) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%.2f", usd(it->price));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", it->quantity);
            ImGui::TableSetColumnIndex(2);
            if (it->buyer_id == kMe) ImGui::TextColored(kGreen, "YOU");
            else ImGui::Text("bot");
            ImGui::TableSetColumnIndex(3);
            if (it->seller_id == kMe) ImGui::TextColored(kRed, "YOU");
            else ImGui::Text("bot");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace

int main() {
    // ---- the market (same engine the console demo uses) ----
    const Price start_price = dollarsToCents(100.00);
    Engine engine("AAPL", start_price);
    PriceFeed feed(engine, start_price);
    engine.start();
    feed.start();
    bool bot_running = true;

    // ---- window and GUI setup ----
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "Could not initialise GLFW\n";
        return 1;
    }
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Stock Simulator", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Could not create the window (is OpenGL available?)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ---- main loop: one iteration = one frame ----
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Copy the latest published state. The engine thread keeps running
        // independently; we never touch the order book directly.
        const MarketSnapshot snap = engine.snapshot();

        drawPricePanel(snap, engine.elapsedSeconds());
        drawBookPanel(snap);
        drawTradePanel(engine, feed, snap, bot_running);
        drawTradesPanel(snap);

        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ---- shutdown: stop the market threads, then tear down the window ----
    feed.stop();
    engine.stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}