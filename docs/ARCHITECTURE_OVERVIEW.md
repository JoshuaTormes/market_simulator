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
│  │ (regime/gbm/ou/  │──▶│  Process          │──▶│  (10 tipos)   │   │
│  │  jump)           │   │  (EventBus)       │   │               │   │
│  └──────────────────┘   └───────────────────┘   └───────┬───────┘   │
│                                                          │ actions  │
│  ┌──────────────────┐   ┌───────────────────────────┐    │          │
│  │ MarketData       │◀──│  MatchingEngine            │◀───┘         │
│  │ Publisher        │   │  (OrderBookV2 + Latency)   │              │
│  └──────────┬───────┘   └───────────────────────────┘              │
│             │ snapshot (+ V e news_cum_log, apenas para o log)      │
└─────────────┼───────────────────────────────────────────────────────┘
              ▼  ILogSink
┌─────────────────────────────────────────────────────────────────────┐
│  BinaryLogWriter (schema v4) ─▶ BinaryLogReader ─┐                  │
│                                                  ├▶ SeriesCollector │
│  sim_calibrate: SeriesCollector ao vivo ─────────┘   ─▶ Report      │
│                 (sem disco)          (price discovery, bateria v2,  │
│                                       legado, metaordem, P&L)       │
└─────────────────────────────────────────────────────────────────────┘
```

`V` entra no `MarketSnapshot` só para ser gravado no log. Os agentes o veem apenas se o perfil de
informação disser (`InformationProfile::sees_fundamental`), o que hoje vale para o informado de
Kyle e para o `LimitOrderTrader`, este com um erro de visão AR(1) próprio. O market maker não vê `V`:
ele mantém uma crença própria atualizada pelo fluxo e pelas notícias públicas do EventBus.

---

## Estrutura de Diretórios

```text
src/
├── core/           — Types, RngService, Clock, TimeScale, Logger, Config, EventBus
├── orderbook/      — OrderBookV2 (preço-tempo), Order, Trade, FeeModel
├── matching/       — MatchingEngine (fila de eventos + latência)
├── agents/         — IAgent, AgentBase, AgentFactory, 10 tipos de agente
├── economics/      — Processos fundamentais (GBM, OU, JumpDiffusion, RegimeSwitching) + Poisson news
├── marketdata/     — MarketDataPublisher, MarketSnapshot, AgentSnapshot, SnapshotBuffer
├── clearing/       — Clearing (aplica trades ao ledger)
├── ledger/         — PositionLedger (posição, P&L e taxas por agente; conta de alocação)
├── risk/           — RiskGate (filtro pré-envio), RiskLimits
├── sim/            — SimulationLoop (orquestração do tick)
├── persistence/    — BinaryLogWriter/Reader, EventSchema (v4), ILogSink, Replay, CsvExporter
├── analysis/       — Report, SeriesCollector, StylizedFactsV2, Protocol, AcfComputer,
│                     HillEstimator, FlashCrashDetector
├── visual/         — VisualApp, painéis ImGui/ImPlot
└── tools/
    ├── sim_headless.cpp       — uma rodada + relatório inline
    ├── sim_calibrate.cpp      — ensemble paralelo em memória, CAL/VAL, pareado, ablações
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
  market maker usa `TTL = snap.base.tick + requote_period + 2`.
- **Os makers não recotam numa grade.** Maker `i` tem período médio `1 + (i % requote_period_max)`,
  e o intervalo até a próxima recotação é **sorteado** uniforme em {1, …, 2p − 1} em toda chamada; o
  TTL é `tick + intervalo + 2`. O OFI é acumulado em todo tick, e na recotação o maker precifica todo
  o fluxo desde a anterior. Cadência fixa, mesmo escalonada em 1/2/3, grava uma grade de período 6 no
  mid; ver [STYLIZED_FACTS_VALIDATION.md](./STYLIZED_FACTS_VALIDATION.md).
- **A ordem de processamento dos agentes é sorteada a cada tick** (`AgentRunner`), a partir de um
  hash de `(seed, tick)` — determinístico, mas sem ordem de chegada privilegiada. Com ordem fixa,
  prioridade temporal dava a um único market maker 72–93% do volume cotado.

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
| `MarketMakerAS` | Reserva por inventário *inspirada* em Avellaneda-Stoikov (sem γ nem horizonte, regras lineares em ticks); crença com λ fixo, passo `λ·sign(OFI)·sqrt(\|OFI\|·q_ref)` sobre o OFI acumulado, mais a notícia do EventBus; cadência sorteada | 3 | 1200 (`q_soft = 600`, fade) |
| `NoiseTrader` | Ordens de mercado Poisson com sazonalidade em U, tilt fraco contra o inventário | 25 | 5000 |
| `InformedTraderKyle` | Kyle (1985); vê `V` com ruído; negocia **posição-alvo**, não fluxo; tamanhos Pareto | 2 | 4000 (`q_soft_frac = 0,6`) |
| `MomentumTrader` | Seguidor de tendência intradiário (crossover de médias móveis em janelas de segundos); ordem = alvo − inventário do ledger | 3 | — |
| `MeanReverterOU` | Entrada por z-score, saída por reversão; ordem = alvo − inventário do ledger | 2 | — |
| `ValueInvestor` | Proporcional ao desvio do fundamental | 0 (desativado) | — |
| `InstitutionalExecutor` | Parents Poisson com sazonalidade em U, fatiamento TWAP com filhos Pareto | 2 | 4000 |
| `StopLossCluster` | Stop por drawdown de 1,5–4%, re-arma após cooldown; coorte 70% comprada (assimetria de stops, não o efeito alavancagem) | 6 | — |
| `NewsReactor` | Uma reação por evento, em tick sorteado na janela; desmonta após holding log-uniforme de 300–3600 s | 8 | 2000 |
| `LimitOrderTrader` | Chiarella-Iori: expectativa = pesos sorteados de fundamentalista (V com erro AR(1)), chartista e ruído; ordem limitada que nunca cruza o topo, TTL = horizonte | 20 | 1000 (`q_soft = 500`) |

**Conta de alocação.** A coorte de stop-loss nasce com posição. Cada posição inicial é registrada
contra uma conta reservada, `kAllocationAccount` (`PositionLedger::seed_pair`), então Σ posições = 0 e
Σ P&L bruto = 0 valem exatamente desde o tick 0, e o relatório confere as duas por seed.

**Limite de perda.** `RiskLimits::max_drawdown_pct = 1,0` desliga o limite na ecologia. O padrão antigo
(0,20 de um nocional de 1e8) era uma barreira absorvente sobre o P&L acumulado: o maker de período 1
cruzava o piso por volta do pregão 40 e saía para sempre. Quem ligar o limite consegue zerar, porque
uma ordem que só aproxima a posição de zero nunca é bloqueada.

Quatro padrões que valem para qualquer agente novo:

- **Tilt de inventário em vez de moeda honesta.** Um agente que sorteia lado com moeda justa faz
  random walk até o limite de posição. Lá o RiskGate rejeita um lado e o fluxo restante vira
  empurrão direcional que ninguém pediu. `p_buy = 0,5 − 0,5·tanh(q / q_scale)` resolve sem
  transformar o ruidoso em informado.
- **Orçamento próprio menor que o limite duro.** O `RiskLimits` é o limite do broker. Um agente que
  usa o limite do broker como orçamento passa a simulação encostado nele e para de decidir.
- **Processo de chegada, não evento único.** Um desk que recebe uma ordem no construtor fica mudo
  depois dos primeiros cem ticks.
- **Posição do ledger, nunca de flag.** `snap.own_inventory` é a posição real. Um flag interno que
  conta ordem enviada como executada desalinha da posição assim que uma ordem não executa.

### Processo fundamental (`src/economics/`)

Quatro processos atrás de `IFundamentalValueProcess`, escolhidos por `--process`. O default é
`RegimeSwitchingProcess` (Hamilton 1989):

```text
Regimes = multiplicadores sobre o σ base por tick, não níveis absolutos, todos com drift zero:
  0 calmo      ×1,0    duração média 7200 s (2 h)
  1 elevado    ×2,5    1800 s (30 min)
  2 alta vol   ×6,0    180 s (3 min)

Matriz de transição derivada das durações e do TimeScale (FundamentalProcessFactory):
  P[fica em i] = 1 − dt/duração_i;  a massa de saída é repartida por regime_exit_split
  (linha = origem):  {—, 0,8, 0,2}  {0,8, —, 0,2}  {0,3, 0,7, —}

σ base = σ_tick / sqrt(Σ π_i m_i²), com π a distribuição estacionária da cadeia, para que a vol
incondicional de V seja sigma_annual.

Innovations: Student-t(ν = 3,5) normalizada a variância unitária, ou N(0,1) com --gaussian
Passo em log-preço: log_s += (μ − 0,5σ²)·dt + σ·√dt·z
```

`mu_annual = 0` — o fundamental é martingale. Um drift diferente de zero é uma afirmação sobre
retorno esperado, não um remendo para um processo que colapsa: com os regimes definidos como
multiplicadores de um σ comum e `mu_annual = 0`, não há drift a compensar. O regime de alta vol é
seis vezes a volatilidade normal, não um crash nem um ativo diferente. Os estados persistem por
horas (Andersen & Bollerslev 1997); uma matriz escrita à mão por tick implicava dezenas de segundos.

Notícias: `PoissonNewsProcess`, 8 anúncios por sessão, impacto log com cauda t(3) e escala 0,273%.
O choque é aplicado ao próprio fundamental (`apply_shock`) e publicado no `EventBus`, então o
market maker reprecifica na hora em vez de ser atropelado por quem leu primeiro. O snapshot grava a
soma acumulada dos choques (`news_cum_log`), o que separa no price discovery a parte de V que vem das
notícias da parte difusiva. A leitura do maker pode ter erro (`news_signal_noise`) ou ser desligada
(`news_blind`), como teste de robustez. Os simuladores montam o `PoissonNewsProcess` a partir do
`NewsConfig` (duração da janela 5 ticks); os padrões da struct do processo não são usados.

**Orçamento de variância.** Como o anúncio entra no mesmo `log V` que a difusão, ele consome parte
da variância de `sigma_annual` em vez de somar por fora (Merton 1976): os pontos de entrada
preenchem `FundamentalConfig::news_var_per_tick` a partir de `NewsConfig::variance_per_tick()`, e a
fábrica entrega à difusão só o resto, com piso em 25% do total. Sem isso, `sigma_annual` não é a
volatilidade de coisa nenhuma — a 0,4% de escala o fluxo de anúncios sozinho carregava 108% de um
orçamento de 30% e o fundamental realizava 43%. A repartição atual é ~50/50 entre difusão e saltos,
e a metade de saltos é o que sustenta o índice de cauda de Hill.

**Sazonalidade intradiária.** `IntradayProfile` (Admati-Pfleiderer 1988) multiplica a taxa de
chegada de noise traders, de parents institucionais e de LOTs por uma parábola em U ao longo da sessão, de
`floor_mult = 0,6` no meio do dia a `peak_mult = 1,8` nas pontas.

### Market data (`src/marketdata/`)

`MarketDataPublisher::publish(tick)` calcula por tick: `mid`, `spread`, `relative_spread`,
`micro_price` (ponderado por tamanho), `weighted_mid`, profundidade e desbalanço em 5 níveis,
`realized_vol` em 3 janelas (Welford online), OFI por tick e em janela, momentum e a fração da
quantidade no topo do book que não é de maker (`OrderBookV2::top_qty_where`).

Quando um lado do book está vazio, o mid é o **último mid formado pelo mercado**
(`prev_valid_mid_`), nunca o fundamental. Ancorar no fundamental produziria price discovery por
construção e a métrica passaria a medir a si mesma.

### Persistência (`src/persistence/`)

Log binário compacto, sem padding, schema **v4**:

```text
[1 byte EventTag][payload]

0x00 FileHeader           — magic 'SMLB', versão, seed, max_ticks
0x01 TradeRecord          — preço, qty, ids de maker/taker, taxas (1/1000 de tick)
0x02 MarketSnapshotRecord — mid, spread, vol, OFI, regime, fundamental_value (v3), news_cum_log (v4)
0x03 NewsEventRecord
0x04 RegimeChangeRecord
0x05 LedgerEventRecord
0x06 MetaorderRecord      — um por parent institucional concluído (v4)
```

O `fundamental_value` no snapshot é o que torna price discovery mensurável offline, e o
`news_cum_log` separa a parte de notícia. `Replay` re-executa o log e confere determinismo bit-a-bit
via SHA-256. O leitor ainda abre logs v2 (campos do fundamental em sentinela, `has_fundamental` falso)
e v3 (`news_cum_log` = NaN, sem metaordens).

O escritor e o coletor implementam `ILogSink`. `sim_calibrate` liga ao loop um `SeriesCollector`, que
monta as séries em memória a partir dos mesmos registros empacotados que iriam para o disco; o log
só é gravado com `--keep-logs`. Relatório de memória e de disco não podem divergir.

**Taxas.** `FeeModel` cobra em bp do nocional (`taker_fee_bp`, `maker_rebate_bp`, padrão 0). A taxa é
contada em unidades de 1/`kFeeUnitsPerTick` (1000) de tick em `Trade`, no ledger e no log, o que
mantém a soma exata em inteiros: 0,1 bp a $100 é 0,1 tick por lote. Nenhum agente vê a taxa.

---

## Análise e Calibração

`Report` é separado em extract (séries a partir do log ou do `SeriesCollector`) e compute (métricas).
Devolve o bloco de price discovery (correlação com V, com a difusão e com as notícias por horizonte,
gap e meia-vida, book de dois lados, volume MM-vs-MM), a bateria v2 (`StylizedFactsV2`: 10 fatos em
retornos de calendário de 1/5/30 min, com IC por bootstrap e estado PASSA/FALHA/INCONCLUSIVO), a
bateria legada de 8 fatos a 1 s (diagnóstico), o δ de metaordem e os contadores de ecologia. Os ids
dos market makers entram para separar churn entre makers de negociação de verdade.

`sim_calibrate` é o harness de ensemble: seeds em paralelo (`--jobs`), média ± SE por métrica, fração
de seeds que passa cada critério, comparação pareada seed a seed (`--baseline`), P&L por classe com
soma zero conferida, ablações (`--set`) e `--gaussian` como teste de emergência. As métricas estão
separadas em CAL (pode orientar ajuste), VAL (hold-out, nunca orienta ajuste) e diag, em
`src/analysis/Protocol.h`. Detalhes e resultados em
[STYLIZED_FACTS_VALIDATION.md](./STYLIZED_FACTS_VALIDATION.md).

Uma seed pode passar em tudo por sorte — foi o que aconteceu com a seed 42 durante a calibração.
O ensemble é a unidade de medida.

---

## CLI

```bash
./build/sim_headless --seed 42 --days 5 --output logs/run.bin
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir /tmp/cal/
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --gaussian --outdir /tmp/gauss/
./build/analysis_csv_export logs/run.bin out/

# Determinismo: os dois hashes têm de ser idênticos
./build/sim_headless --seed 42 --days 5 --output /tmp/A.bin
./build/sim_headless --seed 42 --days 5 --output /tmp/B.bin
shasum -a 256 /tmp/A.bin /tmp/B.bin
```

Não existe arquivo de configuração: os parâmetros vivem em `src/core/Config.h`, junto da
justificativa de cada um. Em runtime: `--process`, `--sigma-annual`, `--news-per-day`, `--gaussian`,
`--news-noise`, `--news-blind`, `--taker-fee`, `--maker-rebate`, e no `sim_calibrate` também
`--seed-set`, `--jobs`, `--baseline`, `--set chave=valor` e `--keep-logs`. Argumento desconhecido sai
com código 2.

---

## Configuração (`src/core/Config.h`)

```cpp
uint64_t  seed                = 42;
uint64_t  max_ticks           = 100'000;
double    taker_fee_bp        = 0.0;     // bp do nocional
double    maker_rebate_bp     = 0.0;
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
| `src/agents/MarketMakerAS.{h,cpp}` | Cotação com reserva por inventário (inspirada em A-S), crença de λ fixo sobre o OFI acumulado, cadência sorteada, TTL |
| `src/agents/LimitOrderTrader.{h,cpp}` | Ordens limitadas Chiarella-Iori que nunca cruzam |
| `src/agents/AgentRunner.{h,cpp}` | Despacho de snapshots, diagnóstico de ticks no limite |
| `src/marketdata/MarketDataPublisher.{h,cpp}` | Snapshot, carry-forward do mid |
| `src/analysis/Report.{h,cpp}` | extract/compute: price discovery, fatos legados, metaordem |
| `src/analysis/StylizedFactsV2.{h,cpp}` | Bateria v2 em retornos de calendário |
| `src/analysis/SeriesCollector.{h,cpp}` | Séries do relatório, ao vivo ou do disco |
| `src/analysis/Protocol.h` | Conjuntos CAL/VAL/diag e faixas de seeds |
| `src/ledger/PositionLedger.{h,cpp}` | Posição, P&L, taxas, `kAllocationAccount` |
| `src/risk/RiskGate.cpp` | Limites de posição e ordem; limite de perda que nunca bloqueia redução |
| `src/tools/sim_calibrate.cpp` | Ensemble, protocolo, pareado, ablações, teste de emergência |
| `src/persistence/EventSchema.h` | Schema binário v4 |
