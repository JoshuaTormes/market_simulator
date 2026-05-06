#include "Engine.h"
#include <fstream>
#include "../agents/RandomAgent.h"
#include "../agents/MarketMakerAgent.h"
#include "../agents/TrendFollowerAgent.h"
#include "../agents/FearfulAgent.h"
#include "../agents/MarketOrderAgent.h"
#include "../agents/MeanReverterAgent.h"
#include "../agents/ValueAgent.h"
#include "../agents/ShockAgent.h"
#include "../agents/InvestorAgent.h"
#include "../agents/ContrarianAgent.h"
#include "../agents/CyclicTraderAgent.h"
#include "../agents/LiquidityConsumerAgent.h"
#include "Clock.h"
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>



Engine::Engine(uint64_t maxTicks, const std::vector<std::string>& tickers)
    : currentTick(0),
      maxTicks(maxTicks),
      nextOrderId(1),
      ticksPerCandle(60),
      candleIndex(0),
      tickers(tickers),
      candleWriter("candles.csv"),
      orderLogger("orders.csv"),
      agentWriter("agents.csv"),
      tradeLogger("trades.csv"),
      positionWriter("positions.csv") ,
      rng(std::random_device{}()) {   


    for (auto& t : tickers) {
        lastPrices[t] = 100.0;
        priceHistories[t].push_back(100.0);
        currentCandles[t] = Candle{
            candleIndex,
            currentTick,
            100.0,
            100.0,
            100.0,
            100.0,
            0
        };
    }

    initializeAgents();
}

void Engine::initializeAgents() {
    uint64_t id = 1;
    double initialPrice = 100.0;
    std::string initialTicker = "AAPL";

    auto mm = std::make_shared<MarketMakerAgent>(
        id++,
        15000.0,
        50,
        0.05,
        50.0,
        0.4
    );
    agents.push_back(mm);
    agentTypes[mm->getId()] = mm->type();
    agentWriter.write(mm->getId(), mm->type());
    // positionLedger.setInitialPosition(mm->getId(), initialTicker, 200, initialPrice);

    {
        auto tf = std::make_shared<TrendFollowerAgent>(
            id++,
            10000.0,
            50,
            5
        );
        agents.push_back(tf);
        agentTypes[tf->getId()] = tf->type();
        agentWriter.write(tf->getId(), tf->type());
        positionLedger.setInitialPosition(tf->getId(), initialTicker, 300, initialPrice);

    }

    {
        auto mr = std::make_shared<MeanReverterAgent>(
            id++,
            8000.0,
            50,
            8,
            0.05
        );

        agents.push_back(mr);
        agentTypes[mr->getId()] = mr->type();
        agentWriter.write(mr->getId(), mr->type());
        positionLedger.setInitialPosition(mr->getId(), initialTicker, 200, initialPrice);

    }
    {
        auto investor = std::make_shared<InvestorAgent>(
            id++,
            12000.0,
            50,
            0.03,
            0.02
        );

        agents.push_back(investor);
        agentTypes[investor->getId()] = investor->type();
        agentWriter.write(investor->getId(), investor->type());
    }

    {
        auto ct = std::make_shared<CyclicTraderAgent>(
            id++,
            8000.0,    // caixa inicial
            50,         // max order size
            5          // executa a cada 5 ticks
        );

        agents.push_back(ct);
        agentTypes[ct->getId()] = ct->type();
        agentWriter.write(ct->getId(), ct->type());
        positionLedger.setInitialPosition(ct->getId(), initialTicker, 50, initialPrice);
    }

    {
        auto contrarian = std::make_shared<ContrarianAgent>(
            id++,
            10000.0,
            50,
            0.3
        );

        agents.push_back(contrarian);
        agentTypes[contrarian->getId()] = contrarian->type();
        agentWriter.write(contrarian->getId(), contrarian->type());
        positionLedger.setInitialPosition(contrarian->getId(), initialTicker, 150, initialPrice);
    }


    {
        auto ra = std::make_shared<RandomAgent>(
            id++,
            8000.0,
            50
        );
        agents.push_back(ra);
        agentTypes[ra->getId()] = ra->type();
        agentWriter.write(ra->getId(), ra->type());
    }

    {
        auto fa = std::make_shared<FearfulAgent>(
            id++,
            7000.0,
            50,
            0.02,
            0.04
        );
        agents.push_back(fa);
        agentTypes[fa->getId()] = fa->type();
        agentWriter.write(fa->getId(), fa->type());
    }

    {
        auto mo = std::make_shared<MarketOrderAgent>(
            id++,
            6000.0,
            50
        );
        agents.push_back(mo);
        agentTypes[mo->getId()] = mo->type();
        agentWriter.write(mo->getId(), mo->type());
        positionLedger.setInitialPosition(mo->getId(), initialTicker, 200, initialPrice);
    }
    {
        auto lc = std::make_shared<LiquidityConsumerAgent>(
            id++,
            9000.0,
            50,
            200,
            30
        );

        agents.push_back(lc);
        agentTypes[lc->getId()] = lc->type();
        agentWriter.write(lc->getId(), lc->type());
        positionLedger.setInitialPosition(lc->getId(), initialTicker, 100, initialPrice);
    }

    {
        auto va = std::make_shared<ValueAgent>(
            id++,
            10000.0,
            50,
            0.025
        );
        agents.push_back(va);
        agentTypes[va->getId()] = va->type();
        agentWriter.write(va->getId(), va->type());
        positionLedger.setInitialPosition(va->getId(), initialTicker, 300, initialPrice);
    }

    for (auto& agent : agents)
        agent->bindLedger(&positionLedger);
}

MarketSnapshot Engine::buildSnapshot(const std::string& ticker) const {
    std::vector<MarketEvent> activeEvents;
    for (const auto& evt : marketEvents) {
        if (evt.getTicker() == ticker && evt.isActive(currentTick))
            activeEvents.push_back(evt);
    }

    double bid = orderBook.bestBid();
    double ask = orderBook.bestAsk();
    double last = lastPrices.at(ticker);

    if (bid <= 0.0) bid = last;
    if (ask <= 0.0) ask = last;

    double bidDepth = 0.0;
    double askDepth = 0.0;

    for (const auto& lvl : orderBook.getBids())
        bidDepth += lvl.quantity;

    for (const auto& lvl : orderBook.getAsks())
        askDepth += lvl.quantity;

    return MarketSnapshot{
        last,
        bid,
        ask,
        bidDepth,
        askDepth,
        priceHistories.at(ticker),
        activeEvents,
        currentTick
    };
}


void Engine::startNewCandle(const std::string& ticker) {
    double last = lastPrices[ticker];
    currentCandles[ticker] = Candle{
        candleIndex,
        currentTick,
        last,
        last,
        last,
        last,
        0
    };
}

void Engine::processTrades(const std::vector<Trade>& trades, const std::string& ticker) {
    if (trades.empty()) return;

    Candle& candle = currentCandles[ticker];

    for (const auto& trade : trades) {
        uint64_t buyAgentId = orderIdToAgentId[trade.buyOrderId];
        uint64_t sellAgentId = orderIdToAgentId[trade.sellOrderId];

        auto* buyAgent = static_cast<AgentBase*>(
            std::find_if(agents.begin(), agents.end(),
                [&](const auto& a){ return a->getId() == buyAgentId; }
            )->get()
        );

        auto* sellAgent = static_cast<AgentBase*>(
            std::find_if(agents.begin(), agents.end(),
                [&](const auto& a){ return a->getId() == sellAgentId; }
            )->get()
        );

        double value = trade.price * trade.quantity;

        buyAgent->debitCash(value);
        sellAgent->creditCash(value);

        positionLedger.applyTrade(
            buyAgentId,
            sellAgentId,
            ticker,
            trade.quantity,
            trade.price
        );

        std::cout << "teste: ";

        tradeLogger.write(
            currentTick,
            ticker,
            trade,
            buyAgentId,
            agentTypes[buyAgentId],
            sellAgentId,
            agentTypes[sellAgentId]
        );

        lastPrices[ticker] = trade.price;
        priceHistories[ticker].push_back(trade.price);

        Candle& candle = currentCandles[ticker];
        candle.high = std::max(candle.high, trade.price);
        candle.low  = std::min(candle.low, trade.price);
        candle.close = trade.price;
        candle.volume += trade.quantity;
    }

    if (candle.low > candle.close) candle.low = candle.close;
    if (candle.high < candle.close) candle.high = candle.close;
}

void Engine::closeCandle(const std::string& ticker) {
    Candle& c = currentCandles[ticker];
    candlesHistory[ticker].push_back(c);
    candleWriter.write(currentCandles[ticker]);
    candleIndex++;
    startNewCandle(ticker);
}

void Engine::generateRandomEvents() {
    std::uniform_real_distribution<double> impactDist(-0.05, 0.05);
    std::uniform_int_distribution<int> durationDist(1, 10);
    std::uniform_real_distribution<double> chanceDist(0.0, 1.0);

    for (auto& ticker : tickers) {
        if (chanceDist(rng) < 0.000009) {
            MarketEvent evt(
                ticker,
                impactDist(rng),
                currentTick,
                currentTick + durationDist(rng)
            );
            marketEvents.push_back(evt);
        }
    }
}

void Engine::spawnRandomAgent() {
    std::uniform_real_distribution<double> spawnDist(0.0, 1.0);
    double spawnChance = 0.002; // 0.2% de chance por tick

    if (spawnDist(rng) >= spawnChance) return;

    // Escolher tipo de agente aleatoriamente
    std::vector<int> types = {1,2,3,4,5}; // 0=MarketMaker,1=TrendFollower,2=MeanReverter,3=Fearful,4=MarketOrder,5=Value
    std::uniform_int_distribution<int> typeDist(0, types.size()-1);
    int chosenType = types[typeDist(rng)];

    uint64_t id = nextOrderId++; 
    std::shared_ptr<AgentBase> newAgent;

    // Funções auxiliares para parâmetros aleatórios
    auto randomCash = [&](){ return 5000.0 + spawnDist(rng)*15000.0; };
    auto randomSize = [&](){ return 3 + int(spawnDist(rng)*15); };
    auto randomThreshold = [&](){ return 0.01 + spawnDist(rng)*0.05; };
    auto randomSpread = [&](){ return 50.0 + spawnDist(rng)*100.0; };
    auto randomLookback = [&](){ return 3 + int(spawnDist(rng)*10); };

    switch(chosenType) {
        case 0: // MarketMaker
            newAgent = std::make_shared<MarketMakerAgent>(id, randomCash(), randomSize(), randomThreshold(), randomSpread(), 0.4);
            break;
        case 1: // TrendFollower
            newAgent = std::make_shared<TrendFollowerAgent>(id, randomCash(), randomSize(), randomLookback());
            break;
        case 2: // MeanReverter
            newAgent = std::make_shared<MeanReverterAgent>(id, randomCash(), randomSize(), randomLookback(), randomThreshold());
            break;
        case 3: // Fearful
            newAgent = std::make_shared<FearfulAgent>(id, randomCash(), randomSize(), randomThreshold(), randomThreshold());
            break;
        case 4: // MarketOrder
            // newAgent = std::make_shared<MarketOrderAgent>(id, randomCash(), randomSize());
            break;
        case 5: // Value
            newAgent = std::make_shared<ValueAgent>(id, randomCash(), randomSize(), randomThreshold());
            break;
    }

    if(!newAgent) return;

    newAgent->bindLedger(&positionLedger);
    agents.push_back(newAgent);
    agentTypes[newAgent->getId()] = newAgent->type();
    agentWriter.write(newAgent->getId(), newAgent->type());
}

void Engine::onTick(uint64_t tick) {
    currentTick = tick;

    for (auto& ticker : tickers) {
        spawnRandomAgent();
        auto snapshot = buildSnapshot(ticker);

        for (auto& agent : agents) {
            auto orders = agent->analisar(snapshot, currentTick, ticker);

            for (auto& order : orders) {
                order.id = nextOrderId++;
                orderIdToAgentId[order.id] = order.agentId;
                std::cout << "orderid" << order.id;


                if (!canPlaceOrder(order))
                    std::cout << "canPlaceOrder:\n";
                
                    continue;

                for (const auto& o : orders) {
                    std::cout << "OrderType=" << static_cast<int>(o.type) << std::endl;

                    std::cout
                        << "id=" << o.id
                        << " agent=" << o.agentId
                        << " side=" << (o.side == Side::Buy ? "BUY" : "SELL")
                        << " type=" << static_cast<int>(o.type)
                        << " price=" << o.price
                        << " qty=" << o.quantity
                        << " tick=" << o.timestamp
                        << " ticker=" << o.ticker
                        << std::endl;
                }

                if (order.type == OrderType::Limit) {
                    std::cout << "added order:\n";

                    orderBook.addOrder(order, currentTick);
                } else {
                    // auto trades = orderBook.executeMarket(order, currentTick);
                    // processTrades(trades, ticker);
                }

                orderLogger.write(currentTick, order, agentTypes[order.agentId]);
            }
        }

        auto trades = orderBook.match(currentTick);

        if (!trades.empty()) {

            processTrades(trades, ticker);
        } else {
            applyBookPressure(ticker);
        }

        if ((currentTick + 1) % ticksPerCandle == 0)
            closeCandle(ticker);
    }

    std::unordered_map<std::string, double> marketPrices;
    for (auto& t : tickers)
        marketPrices[t] = lastPrices[t];

    for (auto& agent : agents)
        agent->recordPnL(currentTick, marketPrices);

    const auto& allPositions = positionLedger.getAllPositions();

    for (auto& agent : agents) {
        uint64_t agentId = agent->getId();

        auto it = allPositions.find(agentId);
        if (it == allPositions.end())
            continue;

        auto* base = static_cast<AgentBase*>(agent.get());

        for (const auto& [ticker, pos] : it->second) {
            positionWriter.write(
                currentTick,
                agentId,
                agentTypes[agentId],
                ticker,
                base->getCash(),
                pos.qty
            );
        }
    }

    writePnLTick("agents_pnl.csv");
}

void Engine::run(double secondsPerTick) {
    Clock clock(maxTicks, secondsPerTick);
    clock.run([this](uint64_t tick){
        while (paused.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        onTick(tick);
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
    for (auto& t : tickers)
        marketPrices[t] = lastPrices[t];

    for (auto& agent : agents) {
        auto rec = agent->getPnLHistory().back();
        file << currentTick << ","
             << agent->getId() << ","
             << agent->type() << ","
             << rec.pnl << "\n";
    }
}

const std::vector<double>& Engine::getPriceHistory(const std::string& ticker) const {
    return priceHistories.at(ticker);
}

const std::vector<Candle>& Engine::getCandles(const std::string& ticker) const {
    static const std::vector<Candle> empty;
    auto it = candlesHistory.find(ticker);
    if (it == candlesHistory.end()) return empty;
    return it->second;
}

void Engine::pause() {
    paused.store(true);
}

void Engine::resume() {
    paused.store(false);
}

bool Engine::isPaused() const {
    return paused.load();
}


void Engine::applyBookPressure(const std::string& ticker) {
    double bid = orderBook.bestBid();
    double ask = orderBook.bestAsk();
    double last = lastPrices[ticker];

    if (bid <= 0.0 || ask <= 0.0)
        return;

    double mid = (bid + ask) * 0.5;

    double bidDepth = 0.0;
    double askDepth = 0.0;

    for (const auto& lvl : orderBook.getBids())
        bidDepth += lvl.quantity;

    for (const auto& lvl : orderBook.getAsks())
        askDepth += lvl.quantity;

    double totalDepth = bidDepth + askDepth;
    if (totalDepth <= 0.0)
        return;

    double imbalance = (bidDepth - askDepth) / totalDepth;

    double spread = ask - bid;
    double spreadPct = spread / last;

    double drift =
        0.15 * imbalance +
        0.05 * spreadPct;

    double newPrice = last * (1.0 + drift);

    newPrice = std::clamp(newPrice, bid, ask);

    if (std::abs(newPrice - last) > 1e-6) {
        lastPrices[ticker] = newPrice;
        priceHistories[ticker].push_back(newPrice);

        Candle& candle = currentCandles[ticker];
        candle.close = newPrice;
        candle.high = std::max(candle.high, newPrice);
        candle.low = std::min(candle.low, newPrice);
    }
}

const OrderBook& Engine::getOrderBook() const {
    return orderBook;
}

OrderBookView Engine::getOrderBookView() const {
    return orderBook.view();
}

int64_t Engine::getOpenSellQty(uint64_t agentId, const std::string& ticker) const {
    int64_t qty = 0;

    auto view = orderBook.view();

    for (const auto& [price, q] : *view.asks) {
        auto tmp = q;
        while (!tmp.empty()) {
            const auto& o = tmp.front();
            if (o.agentId == agentId && o.ticker == ticker)
                qty += o.quantity;
            tmp.pop();
        }
    }

    return qty;
}

bool Engine::canPlaceOrder(const Order& order) const {
    auto it = std::find_if(
        agents.begin(),
        agents.end(),
        [&](const std::shared_ptr<IAgent>& a) {
            return a->getId() == order.agentId;
        }
    );

    if (it == agents.end())
        return false;

    auto* agent = dynamic_cast<AgentBase*>(it->get());
    if (!agent)
        return false;

    if (order.side == Side::Buy) {
        double cost = order.price * order.quantity;
        return agent->getCash() >= cost;
    }

    int64_t pos = positionLedger.getPosition(order.agentId, order.ticker);
    int64_t open = getOpenSellQty(order.agentId, order.ticker);
    return pos - open >= order.quantity;
}
