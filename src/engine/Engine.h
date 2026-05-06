#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <random>
#include <atomic>

#include "../orderbook/OrderBook.h"
#include "../agents/IAgent.h"
#include "../orderbook/MarketSnapshot.h"
#include "../orderbook/Candle.h"
#include "../orderbook/CandleCsvWriter.h"
#include "OrderLogWriter.h"
#include "AgentCsvWriter.h"
#include "../orderbook/TradeLogger.h"
#include "../ledger/PositionLedger.h"
#include "../ledger/PositionCsvWriter.h"
#include "../engine/MarketEvent.h"




class Engine {
public:
    explicit Engine(uint64_t maxTicks, const std::vector<std::string>& tickers);
    void spawnRandomAgent();
    void run(double secondsPerTick = 60.0);
    void onTick(uint64_t tick);
    void addMarketEvent(const MarketEvent& evt);
    const std::vector<double>& getPriceHistory(const std::string& ticker) const;
    const std::vector<Candle>& getCandles(const std::string& ticker) const;
    void pause();
    void resume();
    bool isPaused() const;
    int64_t getOpenSellQty(uint64_t agentId, const std::string& ticker) const;
    bool canPlaceOrder(const Order& order) const;
    const OrderBook& getOrderBook() const;
    OrderBookView getOrderBookView() const;

private:
    void initializeAgents();
    MarketSnapshot buildSnapshot(const std::string& ticker) const;
    void processTrades(const std::vector<Trade>& trades, const std::string& ticker);
    void startNewCandle(const std::string& ticker);
    void closeCandle(const std::string& ticker);
    void generateRandomEvents();
    void writePnLTick(const std::string& filename);
    void applyBookPressure(const std::string& ticker);

    uint64_t currentTick;
    uint64_t maxTicks;
    uint64_t nextOrderId;

    std::vector<std::string> tickers;
    std::unordered_map<std::string, double> lastPrices;
    std::unordered_map<std::string, std::vector<double>> priceHistories;

    uint64_t ticksPerCandle;
    uint64_t candleIndex;
    std::unordered_map<std::string, Candle> currentCandles;
    std::unordered_map<std::string, std::vector<Candle>> candlesHistory;

    std::unordered_map<uint64_t, uint64_t> orderIdToAgentId;
    std::unordered_map<uint64_t, std::string> agentTypes;

    CandleCsvWriter candleWriter;
    OrderLogWriter orderLogger;
    AgentCsvWriter agentWriter;
    TradeLogger tradeLogger;

    PositionLedger positionLedger;
    PositionCsvWriter positionWriter;

    OrderBook orderBook;
    std::vector<std::shared_ptr<IAgent>> agents;

    std::vector<MarketEvent> marketEvents;
    std::mt19937 rng;  
    std::atomic<bool> paused{false};
};
