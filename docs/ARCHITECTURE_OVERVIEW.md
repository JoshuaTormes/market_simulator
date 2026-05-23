# Arquitetura: Simulador de Microestrutura de Mercado

## Visão Geral

Simulador de mercado financeiro de alta fidelidade em C++17 (CMake). Modela a formação de preços tick-a-tick com múltiplos agentes heterogêneos, um order book real com price-time priority e um processo fundamental com regime-switching estocástico.

```text
┌─────────────────────────────────────────────────────────────────────┐
│                         SimulationLoop                              │
│                     (tick order determinístico)                     │
│                                                                     │
│  ┌──────────────────┐   ┌───────────────────┐   ┌───────────────┐  │
│  │ RegimeSwitching  │   │  PoissonNews      │   │  AgentRunner  │  │
│  │ Process          │──▶│  Process          │──▶│  (8 tipos)    │  │
│  │ (fundamental)    │   │  (EventBus)       │   │               │  │
│  └──────────────────┘   └───────────────────┘   └───────┬───────┘  │
│                                                          │ actions  │
│  ┌──────────────────┐   ┌───────────────────────────┐   │          │
│  │ MarketData       │◀──│  MatchingEngine            │◀──┘          │
│  │ Publisher        │   │  (OrderBookV2 + Latency)   │              │
│  └──────────┬───────┘   └───────────────────────────┘              │
│             │ snapshot                                              │
└─────────────┼───────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  BinaryLogWriter  ──▶  BinaryLogReader  ──▶  Report (8 fatos)      │
│  (EventSchema)          (Replay)              (análise pós-sim)     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Estrutura de Diretórios

```text
src/
├── core/           — Tipos primitivos, RNG, Clock, Logger, Config, EventBus
├── orderbook/      — OrderBookV2 (price-time priority), Order, Trade, FeeModel
├── matching/       — MatchingEngine (event queue + latência), Events
├── agents/         — IAgent, AgentBase, AgentFactory, 8 tipos de agente
├── economics/      — Processos fundamentais (GBM, OU, RegimeSwitching, Poisson)
├── marketdata/     — MarketDataPublisher, MarketSnapshot, AgentSnapshot
├── clearing/       — Clearing (aplica trades ao PositionLedger)
├── ledger/         — PositionLedger (P&L por agente)
├── risk/           — RiskGate (filtro pré-envio), RiskLimits
├── sim/            — SimulationLoop (orquestração do tick)
├── persistence/    — BinaryLogWriter/Reader, EventSchema, Replay, CsvExporter
├── analysis/       — AcfComputer, HillEstimator, FlashCrashDetector, Report
├── visual/         — VisualApp, painéis ImGui/ImPlot
└── tools/
    └── sim_headless.cpp  — CLI headless: sim + análise inline
```

---

## Ordem do Tick (SimulationLoop)

Cada tick executa exatamente nesta sequência — qualquer mudança quebra o determinismo:

```text
1.  fundamental.step(dt)          — avança o processo de valor fundamental
2.  news.step(now)                — gera eventos de notícia (EventBus)
2b. flush_pending_news()          — injeta notícias da UI (thread-safe)
3.  runner.run(prev_snap, F_T)    — agentes observam snap anterior, produzem ações
4.  risk_gate.filter(actions)     — filtra por limites de risco
5.  engine.submit(filtered)       — submete ordens ao matching engine
6.  engine.process_until(now)     — drena fila e faz match; cada trade → clearing
7.  engine.expire(now)            — expira ordens por TTL
8.  publisher.publish(now)        — computa MarketSnapshot do livro atual
9.  snap_buf.commit(snap)         — publica para UI e log binário
10. update_agent_state_buf(snap)  — atualiza PnL dos agentes para UI
```

**Importante:** Agentes em step 3 recebem `prev_snap_` (tick anterior) mas o `fundamental_value` atual (já avançado em step 1). Isso permite que MMs ancorem suas cotações no fundamental corrente sem lag.

---

## Camadas do Sistema

### Camada de Tipos Primitivos (`src/core/`)

```cpp
// src/core/Types.h
using Price   = int64_t;   // preço em tick-units (ex: 10000 = $100.00 com tick_size=0.01)
using Qty     = int64_t;   // quantidade em lotes
using Tick    = uint64_t;  // contador de ticks (começa em 0)
using AgentId = uint64_t;
using OrderId = uint64_t;
using SeqNo   = uint64_t;
```

RNG determinístico: `RngService::for_consumer(id)` retorna `mt19937_64` derivado de `seed_seq{global_seed, hash(id)}`. Nenhum `std::random_device` no código de simulação.

### Camada de Order Book (`src/orderbook/`, `src/matching/`)

```text
OrderBookV2
├── bids: std::map<Price, std::list<OrderNode>, greater<>>
├── asks: std::map<Price, std::list<OrderNode>>
└── index: unordered_map<OrderId, (map_iter, list_iter)>  ← cancel O(1)
```

`MatchingEngine` mantém uma `priority_queue<Event>` ordenada por `(arrival_tick, seq_no)`. Ordens de mercado chegam no presente, limit orders podem ter latência simulada (`LatencyProfile`). Suporta: `Limit`, `Market`, `IOC`, `FOK`, `PostOnly`. Self-Trade Prevention via `STPMode`.

### Camada de Agentes (`src/agents/`)

Todos herdam `AgentBase : IAgent`. Interface central:

```cpp
// src/agents/IAgent.h
virtual std::vector<Action> on_market_data(const AgentSnapshot&) = 0;
```

`AgentSnapshot` é derivado de `MarketSnapshot` + `fundamental_value` + perfil de informação do agente (`sees_fundamental`, `sees_regime`, etc.).

Tipos de agente disponíveis:

| Classe | Estratégia | Conta |
| --- | --- | --- |
| `MarketMakerAS` | Avellaneda-Stoikov (2008), spread adaptativo ao vol | 3 |
| `NoiseTrader` | Ordens de mercado aleatórias (Poisson) | 25 |
| `InformedTraderKyle` | Kyle (1985), negocia em direção ao fundamental | 1 |
| `MeanReverterOU` | Entradas z-score, saídas por reversão | 8 |
| `ValueInvestor` | Compra/vende proporcional ao desvio do fundamental | 2 |
| `InstitutionalExecutor` | TWAP slice executor | 0 (desabilitado) |
| `StopLossCluster` | Stop em percentual de perda | 6 |
| `NewsReactor` | Market orders após evento de notícia | 6 |

### Camada de Processo Fundamental (`src/economics/`)

Implementa `IFundamentalValueProcess`. Processo principal: `RegimeSwitchingProcess` (Hamilton 1989).

```text
Regimes (estado Markov):
  0: low_vol   σ=0.003  μ=+7.3e-5   P[stay]=0.990
  1: high_vol  σ=0.010  μ=+7.3e-5   P[stay]=0.940
  2: crash     σ=0.025  μ=-0.010    P[stay]=0.350

Inovações: Student-t(ν=3.5) normalizadas a variância unitária
  z = t_sample / sqrt(ν/(ν-2)) = t_sample / sqrt(2.333)

Passo GBM (em log-preço):
  log_s_t += (μ_r - 0.5·σ_r²)·dt + σ_r·√dt·z
```

**Drift compensatório:** `μ_low = μ_high = +7.3e-5` garante que o drift esperado de longo prazo seja zero:
```text
E[Δlog_s] = π_low·(μ_low - 0.5·σ_low²)
           + π_high·(μ_high - 0.5·σ_high²)
           + π_crash·(μ_crash - 0.5·σ_crash²)
           ≈ 0
```
Sem essa compensação, o regime crash (μ=-0.010, π≈0.6%) acumula -3.0 de drift em 50k ticks, colapsando o fundamental a quase zero.

### Camada de Market Data (`src/marketdata/`)

`MarketDataPublisher::publish(tick)` computa por tick:

- `mid_price`: `(best_bid + best_ask) / 2` se livro two-sided; caso contrário, `fundamental_price_` (âncora no fundamental para evitar viés de bid-ask bounce)
- `realized_vol[3]`: `sqrt(E[r²])` em janelas de 60/300/900 ticks (Welford online)
- `order_flow_imbalance`: volume comprador − vendedor na janela curta
- `spread`, `relative_spread`, `micro_price` (size-weighted), `weighted_mid`

### Camada de Persistência (`src/persistence/`)

Log binário compacto (schema version 1, sem padding):

```text
[1 byte EventTag][N bytes payload]

Tags:
  0x00 FileHeader        — magic 'SMLB', seed, max_ticks
  0x01 TradeRecord       — price, qty, maker/taker IDs, fees
  0x02 MarketSnapshot    — mid, spread, vol, OFI, regime
  0x03 NewsEventRecord   — impacto, duração
  0x04 RegimeChangeRecord
  0x05 LedgerEventRecord
```

`Replay` re-executa o log e verifica determinismo bit-a-bit via SHA-256.

---

## Análise Pós-Simulação: Fatos Estilizados (`src/analysis/`)

`Report::run(bin_path)` valida 8 fatos estilizados de mercados financeiros reais:

```text
┌─────┬──────────────────────────────────────┬────────────────┐
│  #  │ Fato                                 │ Threshold      │
├─────┼──────────────────────────────────────┼────────────────┤
│ 1   │ Fat tails (excess kurtosis)          │ > 1.0          │
│ 2   │ Return ACF ≈ 0 (lag 1)              │ |ACF| < 0.10   │
│ 3   │ Volatility clustering ACF(|r|, 1)   │ > 0.05         │
│ 4   │ Long memory vol (mean ACF|r| 1..10) │ > 0.02         │
│ 5   │ Gain-loss asymmetry (skew < 0)      │ < 0            │
│ 6   │ Hill tail index α                   │ ∈ [1.8, 6.0]   │
│ 7   │ Spread variation (CoV > 0.08)       │ > 0.08         │
│ 8   │ OFI clustering ACF(|OFI|, 1)        │ > 0.03         │
└─────┴──────────────────────────────────────┴────────────────┘
```

Estimadores usados:
- ACF: estimador biased padrão `C(k)/C(0)` (AcfComputer)
- Hill: estimador de cauda `α̂ = (k/sum(log(X_i/X_k)))^{-1}` sobre os 10% maiores `|retornos|` (HillEstimator)
- Spread CoV: apenas ticks com livro two-sided (spread > 0)
- Flash crashes: detectados com limiar 4σ, janelas de 10/30 ticks (FlashCrashDetector, informativo)

**Resultado com seed=42, 50k ticks (calibração de referência):**

```text
ACF(r,1)=0.006  kurtosis=64.9  skew=-1.51  Hill_α=3.12
SpreadCoV=0.40  OFI_ACF=0.99   8/8 fatos validados
```

---

## CLI: sim_headless

```bash
# Rodar simulação headless com análise inline:
./build/sim_headless --seed 42 --duration 50000 --output logs/run.bin

# Exportar para CSV (trades, snapshots, news, regime_changes):
./build/analysis_csv_export logs/run.bin out/

# Teste de determinismo (hashes devem ser idênticos):
./build/sim_headless --seed 42 --duration 50000 --output /tmp/A.bin
./build/sim_headless --seed 42 --duration 50000 --output /tmp/B.bin
shasum -a 256 /tmp/A.bin /tmp/B.bin
```

---

## Configuração da Simulação (`src/core/Config.h`)

Parâmetros raiz em `SimulationConfig`:

```cpp
uint64_t  seed              = 42;
uint64_t  max_ticks         = 100'000;
double    tick_size         = 0.01;       // $0.01 por tick-unit
int64_t   initial_price_ticks = 10'000;  // $100.00
PopulationConfig population;              // contagem e params por tipo de agente
FundamentalConfig fundamental;            // override do RegimeSwitchingProcess
NewsConfig news;                          // lambda, impact_scale
```

---

## Guia: Como adicionar um novo tipo de agente

1. Criar `src/agents/MeuAgente.{h,cpp}` herdando `AgentBase`
1. Implementar `on_market_data(const AgentSnapshot&) → vector<Action>`
1. Adicionar `struct MeuAgenteParams { int count; ... }` em `Config.h` e incluir em `PopulationConfig`
1. Registrar em `AgentFactory::create()` (`src/agents/AgentFactory.cpp`)
1. Adicionar teste unitário em `tests/unit/test_meu_agente.cpp`
1. Verificar: `cmake --build build && ctest --test-dir build`

---

## Referências Arquiteturais

| Arquivo | Responsabilidade |
| --- | --- |
| `src/sim/SimulationLoop.{h,cpp}` | Orquestração tick-a-tick, ordem determinística |
| `src/core/Config.h` | Configuração central de todos os parâmetros |
| `src/economics/RegimeSwitchingProcess.{h,cpp}` | Processo fundamental com 3 regimes, drift compensatório |
| `src/agents/MarketMakerAS.{h,cpp}` | Market maker Avellaneda-Stoikov com spread adaptativo |
| `src/analysis/Report.{h,cpp}` | Validação dos 8 fatos estilizados |
| `src/tools/sim_headless.cpp` | Ponto de entrada CLI, wiring completo sem UI |
| `src/persistence/EventSchema.h` | Schema binário do log (versão 1) |
| `specs/plans/2026-05-06_refatoracao-microestrutura-completa/` | Plano de 11 fases |
