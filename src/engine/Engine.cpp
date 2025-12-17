#include "Engine.h"
#include <fstream>
#include "../agents/RandomAgent.h"
#include "../agents/MarketMakerAgent.h"
#include "../agents/TrendFollowerAgent.h"
#include "../agents/FearfulAgent.h"
#include "../agents/MarketOrderAgent.h"
#include "../agents/MeanReverterAgent.h"

#include "Clock.h"

Engine::Engine(uint64_t maxTicks, const std::vector<std::string>& tickers)
    : currentTick(0),
      maxTicks(maxTicks),
      nextOrderId(1),
      ticksPerCandle(100),
      candleIndex(0),
      tickers(tickers),
      candleWriter("candles.csv"),
      orderLogger("orders.csv"),
      agentWriter("agents.csv") {

    for (auto& t : tickers) {
        lastPrices[t] = 100.0;
        priceHistories[t].push_back(100.0);
        currentCandles[t] = Candle{
            candleIndex,
            currentTick,
            lastPrices[t], // open
            lastPrices[t], // high
            lastPrices[t], // low
            lastPrices[t], // close
            0              // volume
        };
    }

    initializeAgents();
}

void Engine::initializeAgents() {
    uint64_t id = 1;

    auto mm = std::make_shared<MarketMakerAgent>(id++, 50, 0.1);
    agents.push_back(mm);
    agentWriter.write(mm->getId(), mm->type());

    for (int i = 0; i < 5; ++i) {
        auto tf = std::make_shared<TrendFollowerAgent>(id++, 10000.0, 5, 5);
        agents.push_back(tf);
        agentWriter.write(tf->getId(), tf->type());
    }

    for (int i = 0; i < 5; ++i) {
        auto fa = std::make_shared<FearfulAgent>(id++, 5000.0, 8, 0.02, 0.05);
        agents.push_back(fa);
        agentWriter.write(fa->getId(), fa->type());
    }

    for (int i = 0; i < 5; ++i) {
        auto mo = std::make_shared<MarketOrderAgent>(id++, 5000.0, 8); 
        agents.push_back(mo);
        agentWriter.write(mo->getId(), mo->type());
    }

    for (int i = 0; i < 5; ++i) {
        auto mr = std::make_shared<MeanReverterAgent>(id++, 5000.0, 8, 5, 0.02);
        agents.push_back(mr);
        agentWriter.write(mr->getId(), mr->type());
    }

    // for (int i = 0; i < 5; ++i) {
    //     auto ra = std::make_shared<RandomAgent>(id++, 5000.0, 10);
    //     agents.push_back(ra);
    //     agentWriter.write(ra->getId(), ra->type());
    // }
}

MarketSnapshot Engine::buildSnapshot(const std::string& ticker) const {
    auto itPrice = lastPrices.find(ticker);
    auto itHistory = priceHistories.find(ticker);

    return MarketSnapshot{
        itPrice->second,
        orderBook.bestBid(),
        orderBook.bestAsk(),
        itHistory->second
    };
}

void Engine::startNewCandle(const std::string& ticker) {
    double last = lastPrices[ticker];
    currentCandles[ticker] = Candle{
        candleIndex,
        currentTick,
        last,   // open
        last,   // high
        last,   // low
        last,   // close inicial
        0       // volume
    };
}

void Engine::processTrades(const std::vector<Trade>& trades, const std::string& ticker) {
    if (trades.empty()) return;

    Candle& candle = currentCandles[ticker];

    for (const auto& trade : trades) {
        lastPrices[ticker] = trade.price;
        priceHistories[ticker].push_back(trade.price);

        if (trade.price > candle.high) candle.high = trade.price;
        if (trade.price < candle.low) candle.low = trade.price;
        candle.close = trade.price;
        candle.volume += trade.quantity;
    }

    // Garantir que open ≤ high ≥ low ≤ close
    if (candle.low > candle.close) candle.low = candle.close;
    if (candle.high < candle.close) candle.high = candle.close;
}

void Engine::closeCandle(const std::string& ticker) {
    candleWriter.write(currentCandles[ticker]);
    candleIndex++;
    startNewCandle(ticker);
}


void Engine::onTick(uint64_t tick) {
    currentTick = tick;

    for (auto& ticker : tickers) {
        auto snapshot = buildSnapshot(ticker);

        for (auto& agent : agents) {
            auto orderOpt = agent->analisar(snapshot, currentTick, ticker);
            if (orderOpt.has_value()) {
                auto order = orderOpt.value();
                order.id = nextOrderId++;
                orderBook.addOrder(order);
                orderLogger.write(currentTick, order);
            }
        }

        auto trades = orderBook.match(currentTick);
        processTrades(trades, ticker);

        if ((currentTick + 1) % ticksPerCandle == 0) {
            closeCandle(ticker);
        }
    }

    std::unordered_map<std::string, double> marketPrices;
    for (auto& t : tickers) marketPrices[t] = lastPrices[t];

    for (auto& agent : agents) {
        agent->recordPnL(currentTick, marketPrices);
    }
    writePnLTick("agents_pnl.csv"); 

}

void Engine::run(double secondsPerTick) {
    Clock clock(maxTicks, secondsPerTick);
    clock.run([this](uint64_t tick){
        this->onTick(tick);
    });
}

void Engine::writePnLTick(const std::string& filename) {
    static bool headerWritten = false;
    std::ofstream file(filename, std::ios::app);

    if (!headerWritten) {
        file << "tick,agentId,agentType,pnl\n";
        headerWritten = true;
    }

    std::unordered_map<std::string, double> marketPrices;
    for (auto& t : tickers) marketPrices[t] = lastPrices[t];

    for (auto& agent : agents) {
        agent->recordPnL(currentTick, marketPrices);
        double pnl = agent->getPnLHistory().back().pnl;
        uint64_t tick = agent->getPnLHistory().back().tick; 
        file << currentTick << "," << agent->getId() << "," << agent->type() << "," << pnl << "\n";
    }
}