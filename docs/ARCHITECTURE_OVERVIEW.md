# Arquitetura: Simulador de Microestrutura de Mercado

## Visão Geral

Simulador de mercado em C++17 (CMake). O preço é **endógeno**: existe um valor fundamental latente
`V`, mas nenhum componente escreve o preço negociado — ele emerge do cruzamento de ordens em um
order book real com prioridade preço-tempo. O que se mede depois é se o mid consegue rastrear `V`.

```text
┌─────────────────────────────────────────────────────────────────────┐
│                         SimulationLoop                              │
│                     (tick order determinístico)                     │
│                                                                     │
│  ┌──────────────────┐   ┌───────────────────┐   ┌───────────────┐   │
│  │ Fundamental      │   │  PoissonNews      │   │  AgentRunner  │   │
│  │ (regime/gbm/ou/  │──▶│  Process          │──▶│  (9 tipos)    │   │
│  │  jump)           │   │  (EventBus)       │   │               │   │
│  └──────────────────┘   └───────────────────┘   └───────┬───────┘   │
│                                                          │ actions  │
│  ┌──────────────────┐   ┌───────────────────────────┐    │          │
│  │ MarketData       │◀──│  MatchingEngine            │◀───┘         │
│  │ Publisher        │   │  (OrderBookV2 + Latency)   │              │
│  └──────────┬───────┘   └───────────────────────────┘              │
│             │ snapshot (+ V, apenas para o log)                     │
└─────────────┼───────────────────────────────────────────────────────┘
              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  BinaryLogWriter ─▶ BinaryLogReader ─▶ Report (price discovery +    │
│  (EventSchema v3)      (Replay)         8 fatos)  ─▶ sim_calibrate  │
└─────────────────────────────────────────────────────────────────────┘
```

`V` entra no `MarketSnapshot` só para ser gravado no log. Os agentes o veem apenas se o perfil de
informação disser (`InformationProfile::sees_fundamental`), o que hoje vale só para o informado de
Kyle. O market maker não vê `V`: ele mantém uma crença própria atualizada pelo fluxo.

---

## Estrutura de Diretórios

```text
src/
├── core/           — Types, RngService, Clock, TimeScale, Logger, Config, EventBus
├── orderbook/      — OrderBookV2 (preço-tempo), Order, Trade, FeeModel
├── matching/       — MatchingEngine (fila de eventos + latência)
├── agents/         — IAgent, AgentBase, AgentFactory, 9 tipos de agente
├── economics/      — Processos fundamentais (GBM, OU, JumpDiffusion, RegimeSwitching) + Poisson news
├── marketdata/     — MarketDataPublisher, MarketSnapshot, AgentSnapshot, SnapshotBuffer
├── clearing/       — Clearing (aplica trades ao ledger)
├── ledger/         — PositionLedger (posição e P&L por agente)
├── risk/           — RiskGate (filtro pré-envio), RiskLimits
├── sim/            — SimulationLoop (orquestração do tick)
├── persistence/    — BinaryLogWriter/Reader, EventSchema (v3), Replay, CsvExporter
├── analysis/       — AcfComputer, HillEstimator, FlashCrashDetector, Report
├── visual/         — VisualApp, painéis ImGui/ImPlot
└── tools/
    ├── sim_headless.cpp       — uma rodada + relatório inline
    ├── sim_calibrate.cpp      — ensemble de N seeds, teste de emergência
    └── analysis_csv_export.cpp
```

---

## Escala de Tempo

Toda grandeza com dimensão temporal deriva de `TimeScale`, em `src/core/Config.h`:

```cpp
double seconds_per_tick = 1.0;      // 1 tick = 1 segundo de negociação
double seconds_per_day  = 23400.0;  // sessão de 6,5 h
double trading_days     = 252.0;
```

Daí saem `per_tick_vol(sigma_annual) = sigma_annual / sqrt(ticks_per_year)` e
`per_tick_rate(por_dia)`. Com `sigma_annual = 0,30`, σ por tick ≈ 1,24e-4. Mudar
`seconds_per_tick` reescala o modelo inteiro de forma coerente, em vez de invalidar meia dúzia de
constantes escolhidas à mão.

---

## Ordem do Tick (`SimulationLoop::tick_once`)

Sequência fixa; qualquer mudança altera o resultado bit-a-bit:

```text
1. fundamental.step(dt)             — avança V
2. news.step(now)                   — eventos no EventBus + fundamental.apply_shock(impacto)
3. runner.run(prev_snap_, V, now)   — agentes observam o snapshot do tick anterior
4. risk_gate.filter(actions)        — limites de posição e de ordem
5. engine.submit(filtered)
6. engine.process_until(now)        — casa ordens; cada trade → clearing.apply
7. engine.expire(now)               — expira ordens por TTL
8. publisher.publish(now)           — monta o MarketSnapshot do book atual
9. snap_buf.commit(snap)            — UI e log binário
```

Duas consequências que já causaram bug e por isso estão fixadas em teste:

- Agentes veem `prev_snap_`, cujo `tick == now - 1`. Um agente que assume ver o presente está
  errado por um tick.
- `expire(now)` roda **antes** de `publish(now)`. Uma ordem enviada no tick `now-1` chega em `now`;
  se o TTL fosse `now`, ela seria expirada antes de aparecer em qualquer snapshot. É por isso que o
  market maker usa `TTL = snap.base.tick + 3`.

---

## Camadas

### Tipos e RNG (`src/core/`)

```cpp
using Price   = int64_t;   // tick-units: 10000 = $100.00 com tick_size = 0.01
using Qty     = int64_t;
using Tick    = uint64_t;
using AgentId = uint64_t;
```

`RngService::for_consumer(id)` devolve um `mt19937_64` derivado de `seed_seq{global_seed, hash(id)}`.
Nenhum `std::random_device`. **O stream não pode depender do estado**: um sorteio feito só sob
condição (posição, inventário) desloca todos os sorteios seguintes e a comparação entre duas
rodadas deixa de significar algo. A regra é sortear sempre e decidir depois.

### Order book e matching

```text
OrderBookV2
├── bids: std::map<Price, std::list<OrderNode>, greater<>>
├── asks: std::map<Price, std::list<OrderNode>>
└── index: unordered_map<OrderId, (map_iter, list_iter)>   ← cancel O(1)
```

`MatchingEngine` mantém uma `priority_queue<Event>` ordenada por `(arrival_tick, seq_no)`. Tipos:
`Limit`, `Market`, `IOC`, `FOK`, `PostOnly`. Self-trade prevention via `STPMode`.

### Agentes (`src/agents/`)

Todos herdam `AgentBase : IAgent`, com uma única função de decisão:

```cpp
virtual std::vector<Action> on_market_data(const AgentSnapshot&) = 0;
```

`AgentSnapshot` é o `MarketSnapshot` filtrado pelo perfil de informação do agente, mais a própria
posição (`own_inventory`, `own_avg_cost`) lida do ledger.

| Classe | Estratégia | Conta | Limite de posição |
| --- | --- | --- | --- |
| `MarketMakerAS` | Avellaneda-Stoikov; crença via OFI (Glosten-Milgrom) | 3 | `q_soft = 600` (fade) |
| `NoiseTrader` | Ordens de mercado Poisson, tilt fraco contra o inventário | 25 | 5000 |
| `InformedTraderKyle` | Kyle (1985); vê `V` com ruído; tamanhos Pareto | 3 | 4000 (`q_soft_frac = 0,6`) |
| `MomentumTrader` | Crossover de médias móveis | 3 | — |
| `MeanReverterOU` | Entrada por z-score, saída por reversão | 2 | — |
| `ValueInvestor` | Proporcional ao desvio do fundamental | 0 (desativado) | — |
| `InstitutionalExecutor` | Parents Poisson, fatiamento TWAP com filhos Pareto | 2 | 4000 |
| `StopLossCluster` | Stop por drawdown de 1,5–4%, re-arma após cooldown | 6 | — |
| `NewsReactor` | Uma reação por evento, em tick sorteado na janela | 8 | 2000 |

Três padrões que valem para qualquer agente novo:

- **Tilt de inventário em vez de moeda honesta.** Um agente que sorteia lado com moeda justa faz
  random walk até o limite de posição. Lá o RiskGate rejeita um lado e o fluxo restante vira
  empurrão direcional que ninguém pediu. `p_buy = 0,5 − 0,5·tanh(q / q_scale)` resolve sem
  transformar o ruidoso em informado.
- **Orçamento próprio menor que o limite duro.** O `RiskLimits` é o limite do broker. Um agente que
  usa o limite do broker como orçamento passa a simulação encostado nele e para de decidir.
- **Processo de chegada, não evento único.** Um desk que recebe uma ordem no construtor fica mudo
  depois dos primeiros cem ticks.

### Processo fundamental (`src/economics/`)

Quatro processos atrás de `IFundamentalValueProcess`, escolhidos por `--process`. O default é
`RegimeSwitchingProcess` (Hamilton 1989):

```text
Regimes = multiplicadores sobre o σ base por tick, não níveis absolutos:
  0 quieto    ×1,0    P[fica] = 0,990   (duração média ~100 ticks)
  1 elevado   ×2,5    P[fica] = 0,970   (~33 ticks)
  2 crash     ×6,0    P[fica] = 0,850   (~6,7 ticks)

Matriz de transição (linha = origem):
  {0,990  0,007  0,003}
  {0,020  0,970  0,010}
  {0,050  0,100  0,850}

Innovations: Student-t(ν = 3,5) normalizada a variância unitária, ou N(0,1) com --gaussian
Passo em log-preço: log_s += (μ − 0,5σ²)·dt + σ·√dt·z
```

`mu_annual = 0` — o fundamental é martingale. Um drift diferente de zero é uma afirmação sobre
retorno esperado, não um remendo para um processo que colapsa: com os regimes definidos como
multiplicadores de um σ comum e `mu_annual = 0`, não há drift a compensar. Um crash é seis vezes a
volatilidade normal, não um ativo diferente.

Notícias: `PoissonNewsProcess`, 8 anúncios por sessão, impacto log com cauda t(3) e escala 0,4%.
O choque é aplicado ao próprio fundamental (`apply_shock`) e publicado no `EventBus`, então o
market maker reprecifica na hora em vez de ser atropelado por quem leu primeiro.

### Market data (`src/marketdata/`)

`MarketDataPublisher::publish(tick)` calcula por tick: `mid`, `spread`, `relative_spread`,
`micro_price` (ponderado por tamanho), `weighted_mid`, profundidade e desbalanço em 5 níveis,
`realized_vol` em 3 janelas (Welford online), OFI por tick e em janela, momentum.

Quando um lado do book está vazio, o mid é o **último mid formado pelo mercado**
(`prev_valid_mid_`), nunca o fundamental. Ancorar no fundamental produziria price discovery por
construção e a métrica passaria a medir a si mesma.

### Persistência (`src/persistence/`)

Log binário compacto, sem padding, schema **v3**:

```text
[1 byte EventTag][payload]

0x00 FileHeader          — magic 'SMLB', versão, seed, max_ticks
0x01 TradeRecord         — preço, qty, ids de maker/taker, taxas
0x02 MarketSnapshotRecord — mid, spread, vol, OFI, regime, fundamental_value  ← v3
0x03 NewsEventRecord
0x04 RegimeChangeRecord
0x05 LedgerEventRecord
```

O `fundamental_value` no snapshot é o que torna price discovery mensurável offline. `Replay`
re-executa o log e confere determinismo bit-a-bit via SHA-256; o leitor ainda abre logs v2, onde os
campos dependentes do fundamental ficam em sentinela e `has_fundamental` é falso.

---

## Análise e Calibração

`Report::run(bin_path, mm_ids)` devolve os 8 fatos estilizados **e** o bloco de price discovery
(correlação por horizonte, gap e meia-vida, book de dois lados, volume MM-vs-MM). Os ids dos market
makers entram para separar churn entre makers de negociação de verdade.

`sim_calibrate` é o harness de ensemble: N seeds, média ± desvio por métrica, fração de seeds que
passa cada critério, pior tipo de agente no limite, e `--gaussian` como teste de emergência.
Detalhes e resultados em [STYLIZED_FACTS_VALIDATION.md](./STYLIZED_FACTS_VALIDATION.md).

Uma seed pode passar em tudo por sorte — foi o que aconteceu com a seed 42 durante a calibração.
O ensemble é a unidade de medida.

---

## CLI

```bash
./build/sim_headless --seed 42 --duration 50000 --output logs/run.bin
./build/sim_calibrate --seeds 10 --duration 50000 --outdir /tmp/cal/
./build/sim_calibrate --seeds 10 --duration 50000 --gaussian
./build/analysis_csv_export logs/run.bin out/

# Determinismo: os dois hashes têm de ser idênticos
./build/sim_headless --seed 42 --duration 50000 --output /tmp/A.bin
./build/sim_headless --seed 42 --duration 50000 --output /tmp/B.bin
shasum -a 256 /tmp/A.bin /tmp/B.bin
```

Não existe arquivo de configuração: os parâmetros vivem em `src/core/Config.h`, junto da
justificativa de cada um, e o que é ajustável em runtime está nas flags
(`--process`, `--sigma-annual`, `--news-per-day`, `--gaussian`).

---

## Configuração (`src/core/Config.h`)

```cpp
uint64_t  seed                = 42;
uint64_t  max_ticks           = 100'000;
TimeScale time;                          // 1 tick = 1 s
double    tick_size           = 0.01;
int64_t   initial_price_ticks = 10'000;  // $100.00
int       publish_interval_ticks = 1;
PopulationConfig  population;            // contagem e params por tipo de agente
FundamentalConfig fundamental;           // tipo, sigma_annual, regimes
NewsConfig        news;                  // eventos/dia, escala de impacto
```

Os parâmetros que não devem mudar sem re-rodar o ensemble estão tabelados em
[../CLAUDE.md](../CLAUDE.md).

---

## Como adicionar um tipo de agente

1. Criar `src/agents/MeuAgente.{h,cpp}` herdando `AgentBase`.
2. Implementar `on_market_data(const AgentSnapshot&) → vector<Action>`.
3. Adicionar `struct MeuAgenteParams { int count; ... }` em `Config.h` e incluir em `PopulationConfig`.
4. Registrar em `AgentFactory::create_all()`, com `RiskLimits` vindo do config.
5. Sortear sempre no mesmo lugar do stream (ver a regra de RNG acima).
6. Teste em `tests/unit/test_meu_agente.cpp` + `add_sim_test(...)` em `tests/CMakeLists.txt`.
7. Re-rodar o ensemble de 10 seeds: um agente novo muda a ecologia inteira, não só a si mesmo.

---

## Referências de Código

| Arquivo | Responsabilidade |
| --- | --- |
| `src/sim/SimulationLoop.{h,cpp}` | Ordem do tick, determinismo |
| `src/core/Config.h` | Parâmetros centrais e a escala de tempo |
| `src/economics/RegimeSwitchingProcess.{h,cpp}` | Fundamental com 3 regimes multiplicativos |
| `src/agents/MarketMakerAS.{h,cpp}` | Cotação Avellaneda-Stoikov, crença via OFI, TTL |
| `src/agents/AgentRunner.{h,cpp}` | Despacho de snapshots, diagnóstico de ticks no limite |
| `src/marketdata/MarketDataPublisher.{h,cpp}` | Snapshot, carry-forward do mid |
| `src/analysis/Report.{h,cpp}` | Price discovery + 8 fatos |
| `src/tools/sim_calibrate.cpp` | Ensemble e teste de emergência |
| `src/persistence/EventSchema.h` | Schema binário v3 |
