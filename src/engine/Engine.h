#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include "../orderbook/OrderBook.h"
#include "../agents/IAgent.h"
#include "../orderbook/MarketSnapshot.h"
#include "../orderbook/Candle.h"
#include "../orderbook/CandleCsvWriter.h"
#include "OrderLogWriter.h"
#include "AgentCsvWriter.h"

class Engine {
public:
    explicit Engine(uint64_t maxTicks, const std::vector<std::string>& tickers);
    void run(double secondsPerTick = 60.0);
    void onTick(uint64_t tick);

private:
    void initializeAgents();
    MarketSnapshot buildSnapshot(const std::string& ticker) const;
    void processTrades(const std::vector<Trade>& trades, const std::string& ticker);
    void startNewCandle(const std::string& ticker);
    void closeCandle(const std::string& ticker);
    void writePnLTick(const std::string& filename);

    uint64_t currentTick;
    uint64_t maxTicks;
    uint64_t nextOrderId;

    std::vector<std::string> tickers;
    std::unordered_map<std::string, double> lastPrices;
    std::unordered_map<std::string, std::vector<double>> priceHistories;

    uint64_t ticksPerCandle;
    uint64_t candleIndex;
    std::unordered_map<std::string, Candle> currentCandles;

    CandleCsvWriter candleWriter;
    OrderLogWriter orderLogger;
    AgentCsvWriter agentWriter;

    OrderBook orderBook;
    std::vector<std::shared_ptr<IAgent>> agents;
};
