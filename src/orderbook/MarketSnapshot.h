#pragma once
#include <vector>
#include <cstdint>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <string>
#include "../engine/MarketEvent.h"

struct Level {
    double price;
    double volume;
};

struct MarketSnapshot {
    // --- CAMPOS ORIGINAIS (Compatibilidade Total) ---
    double lastPrice;
    double perceivedPrice;
    double bestBid;
    double bestAsk;
    double bidDepth;      // Antigo bidDepth
    double askDepth;      // Antigo askDepth
    double imbalance;     // (bidDepth - askDepth) / total
    double liquidityStress;
    double spreadPressure;
    std::vector<double> recentPrices;

    // --- NOVOS CAMPOS (Microestrutura e Fluxo) ---
    double bookImbalance;  
    double midPrice;
    double relativeSpread;
    std::vector<Level> bids; // Livro nível 2
    std::vector<Level> asks;
    double buyVolumeLastWindow;
    double sellVolumeLastWindow;
    double tradeAggression; // Ratio 0.0 a 1.0
    double vwap;
    double volatility;
    double priceMomentum;
    bool isStagnant;

    MarketSnapshot(
        double last,
        double bid,
        double ask,
        double bDepth,
        double aDepth,
        const std::vector<double>& hist,
        const std::vector<MarketEvent>& events = {},
        uint64_t currentTick = 0
    ) : lastPrice(last), bestBid(bid), bestAsk(ask), 
        bidDepth(bDepth), askDepth(aDepth), recentPrices(hist) 
    {
        // 1. Lógica Original de Perceived Price e Eventos
        perceivedPrice = lastPrice;
        for (const auto& evt : events) {
            if (evt.isActive(currentTick)) {
                perceivedPrice *= (1.0 + evt.getImpact());
            }
        }

        // 2. Cálculos de Stress e Pressure originais
        double totalDepth = bidDepth + askDepth;
        imbalance = totalDepth > 0.0 ? (bidDepth - askDepth) / totalDepth : 0.0;
        bookImbalance = imbalance; 
        
        double maxD = std::max(bidDepth, askDepth);
        double minD = std::min(bidDepth, askDepth);
        liquidityStress = maxD > 0.0 ? 1.0 - (minD / maxD) : 1.0;
        spreadPressure = lastPrice > 0.0 ? (bestAsk - bestBid) / lastPrice : 0.0;

        // 3. Inicialização das métricas novas (Valores default)
        midPrice = (bestBid + bestAsk) / 2.0;
        relativeSpread = spreadPressure; 
        tradeAggression = 0.5;
        volatility = 0.0;
        priceMomentum = 0.0;
        isStagnant = (bestAsk - bestBid) > (lastPrice * 0.01);
    }

    // Função para o motor atualizar os dados de fluxo sem quebrar o construtor
    void updateMicrostructure(double buyVol, double sellVol, double currentVwap) {
        buyVolumeLastWindow = buyVol;
        sellVolumeLastWindow = sellVol;
        vwap = currentVwap;

        double totalTraded = buyVolumeLastWindow + sellVolumeLastWindow;
        if (totalTraded > 0) {
            tradeAggression = buyVolumeLastWindow / totalTraded;
            isStagnant = false;
        } else {
            isStagnant = true;
        }

        if (recentPrices.size() > 1) {
            calculateStats();
        }
    }

private:
    void calculateStats() {
        double sum = std::accumulate(recentPrices.begin(), recentPrices.end(), 0.0);
        double mean = sum / recentPrices.size();
        double sq_sum = 0.0;
        for (double p : recentPrices) sq_sum += (p - mean) * (p - mean);
        
        volatility = std::sqrt(sq_sum / recentPrices.size());
        priceMomentum = (recentPrices.back() - recentPrices.front()) / recentPrices.front();
    }
};