#include "MeanReverterAgent.h"
#include <numeric>
#include <cmath>
#include <algorithm>

MeanReverterAgent::MeanReverterAgent(
    uint64_t id,
    double cash,
    int maxSize,
    int lookback,
    double threshold
) : AgentBase(id, cash, maxSize), lookback(lookback), threshold(threshold) {}

std::vector<Order> MeanReverterAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    // 1. Validação de dados (Usando a nova estrutura de preços do Snapshot)
    // Se o seu Snapshot atualizado usa 'recentPrices', mantemos. 
    // Se usa 'priceHistory', ajuste o nome abaixo.
    if (snapshot.recentPrices.size() < static_cast<size_t>(lookback))
        return orders;

    if (snapshot.bestAsk <= 0.0 || snapshot.bestBid <= 0.0)
        return orders;

    // 2. Cálculo da Média Móvel (SMA)
    double sum = std::accumulate(
        snapshot.recentPrices.end() - lookback,
        snapshot.recentPrices.end(),
        0.0
    );
    double sma = sum / lookback;
    double mid = snapshot.midPrice; // Usando o midPrice já calculado no Snapshot
    double deviation = mid - sma;

    Position pos = getPosition(ticker);
    double avgPrice = getAvgEntryPrice(ticker);

    // 3. Lógica de Reação ao Fluxo (O "Pulo do Gato")
    // Se o desvio for 80% do threshold mas o fluxo está confirmando a volta, agimos antes.
    double urgentThreshold = threshold * 0.8;
    
    // Fatores de Urgência:
    // tradeAggression > 0.6 (Compradores batendo forte)
    // bookImbalance > 0.5 (Muitas ordens de compra acumuladas no book)
    bool bullishFlow = (snapshot.tradeAggression > 0.6) || (snapshot.bookImbalance > 0.5);
    bool bearishFlow = (snapshot.tradeAggression < 0.4) || (snapshot.bookImbalance < -0.5);

    // 4. Decisão de Compra (Buy)
    bool buySetup = (deviation < -threshold);
    bool urgentBuy = (deviation < -urgentThreshold && bullishFlow);

    if (buySetup || urgentBuy) {
        int maxQty = std::min(maxOrderSize, static_cast<int>(cash / snapshot.bestAsk));
        if (maxQty > 0) {
            // Se o mercado estiver estagnado, postamos uma Limit Order no Mid para "atiçar" o mercado
            double executionPrice = snapshot.isStagnant ? snapshot.midPrice : snapshot.bestAsk;
            
            if (auto o = criarOrdemCompra(ticker, executionPrice, maxQty, tick))
                orders.push_back(*o);
        }
    }

    // 5. Decisão de Venda (Sell/Exit)
    // Adicionamos um "Panic Sell" se o desvio for contra nós e houver fluxo pesado de venda
    bool sellSetup = (deviation > threshold && mid > avgPrice);
    bool stopLoss = (deviation < -threshold * 2.0 && bearishFlow); // Proteção contra queda infinita

    if ((sellSetup || stopLoss) && pos.qty > 0) {
        int maxQty = std::min<int64_t>(pos.qty, maxOrderSize);
        if (maxQty > 0) {
            // Na venda, se houver spread alto, tentamos vender no bid para garantir saída
            double executionPrice = snapshot.bestBid;
            
            if (auto o = criarOrdemVenda(ticker, executionPrice, maxQty, tick))
                orders.push_back(*o);
        }
    }

    // 6. Proatividade em Mercados Parados (Anti-Tick Vazio)
    // Se não há ordens e o spread está aberto, o agente atua como "Market Maker" temporário
    if (orders.empty() && snapshot.isStagnant && pos.qty == 0) {
        // Coloca ordens passivas pequenas para gerar volume
        orders.push_back(*criarOrdemCompra(ticker, snapshot.bestBid, 1, tick));
        orders.push_back(*criarOrdemVenda(ticker, snapshot.bestAsk, 1, tick));
    }

    return orders;
}