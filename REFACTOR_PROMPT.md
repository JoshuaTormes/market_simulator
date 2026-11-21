# PROMPT — Refatoração para Simulador de Mercado de Nível Profissional

> Cole este prompt inteiro numa nova conversa Claude Code dentro do diretório `simulacao_mercado_3`.

---

## CONTEXTO

Você está dentro do projeto `simulacao_mercado_3`, um simulador de mercado em C++17 (CMake + SFML + ImGui + ImPlot) com agentes heterogêneos atuando sobre um order book de duplo leilão contínuo. O código atual é um protótipo didático — funciona, mas tem bugs estruturais, sem rigor matemático, sem reprodutibilidade, sem validação contra fatos estilizados de mercados reais, com race conditions entre a thread de simulação e a UI, e com agentes que leem campos de microestrutura que nunca são populados.

O objetivo é transformar esse protótipo num **simulador agent-based de microestrutura de mercado de nível profissional** — algo onde eu possa **observar fenômenos reais**: volatility clustering, fat tails, leverage effect, flash crashes, spreads que se abrem em estresse, formação de tendência por feedback, momentum crashes, liquidity cascades, regime shifts, etc. Quero um **mercado vivo**, com fundação matemática rigorosa, conservação contábil estrita, latência realista, e agentes cujo comportamento esteja calibrado contra a literatura de market microstructure (Hasbrouck, O'Hara, Kyle, Avellaneda-Stoikov, Cont, Lillo-Farmer, etc).

---

## METODOLOGIA OBRIGATÓRIA

### 1. PRIMEIRO PASSO: invoque a skill `spec-plan`

Antes de tocar em qualquer arquivo de código, **invoque a skill `spec-plan`** para conduzir o workflow Spec Driven Development + Plan. Esta refatoração é grande (vários módulos, fluxos novos, mudanças arquiteturais profundas) — exige spec formal e plano por fases, não improviso.

A spec deve cobrir todos os requisitos abaixo, organizados em fases. Cada fase deve ter critérios de aceitação verificáveis (testes, métricas, fatos estilizados a reproduzir).

### 2. ÚLTIMO PASSO: invoque a skill `doc-generator`

Quando todas as fases estiverem implementadas, validadas e o sistema estiver rodando produzindo os fenômenos esperados, **invoque a skill `doc-generator`** para gerar/atualizar a documentação arquitetural completa em `docs/` — incluindo diagramas de fluxo de dados, ciclo de vida do tick, modelo de threading, contrato de cada agente, glossário de microestrutura, e referências da literatura usada na calibração.

---

## DIAGNÓSTICO DO ESTADO ATUAL — NÃO REPITA ESTES ERROS

Faça uma leitura completa antes de começar. Pontos críticos do código atual que **devem ser corrigidos ou redesenhados**:

1. **Bug de fluxo em [src/engine/Engine.cpp](src/engine/Engine.cpp):** o `if (!canPlaceOrder(order))` está sem chaves, fazendo o `continue` executar incondicionalmente e o loop interno de log rodar fora do escopo correto. Resultado: ordens são adicionadas só por coincidência. Refatore o ciclo `onTick` do zero.
2. **Matching engine ingênuo em [src/orderbook/OrderBook.cpp](src/orderbook/OrderBook.cpp):** preço de execução sempre = `askIt->first`. Deveria seguir **price-time priority** correta com preço do maker (resting order). FIFO não é rastreado por timestamp real. Sem self-trade prevention robusta. Sem cancel-by-id. TTL fixo (60 ticks) hardcoded substitui cancel real.
3. **`applyBookPressure`:** sintetiza preço sem trade real, quebrando a invariante "todo preço novo no histórico veio de um trade". Remova ou substitua por mid-price update separado.
4. **`MarketSnapshot::updateMicrostructure` nunca é chamado pelo Engine**, mas os agentes (MeanReverter, etc) leem `tradeAggression`, `bookImbalance`, `vwap`, `isStagnant`, `volatility`, `priceMomentum` como se fossem reais. Toda a microestrutura está fictícia.
5. **`PositionLedger` não suporta short selling.** Quando vendedor não tem posição, ignora silenciosamente — viola conservação. O ativo é "criado" pelo book e perdido pelo ledger.
6. **Race conditions:** `VisualApp` lê `getPriceHistory`/`getCandles`/`getOrderBookView` da thread principal enquanto a sim thread escreve. Sem mutex, sem double-buffering, sem snapshot atômico.
7. **I/O síncrono no hot path:** todo tick escreve em `candles.csv`, `orders.csv`, `trades.csv`, `positions.csv`, `agents_pnl.csv`. Inviável em escala.
8. **Sem tick size, sem lot size, sem fees, sem latência, sem rejeição por limite de risco, sem priority queue real por (price, time).**
9. **Modelo de "perceived price" é multiplicativo trivial.** Em mercado real, news/eventos afetam crenças heterogêneas por agente, com decaimento, com ruído idiossincrático.
10. **Sem âncora econômica:** não há fundamental value (GBM, OU process, jump-diffusion) drivando expectativas. Tudo emerge do book sem driver exógeno estruturado.
11. **`std::cout` espalhado** no hot path do Engine.
12. **`Clock` é template inline no header com loop trivial** — não há controle de tempo real, throttle, ou modo "as fast as possible" vs "real-time replay".
13. **Sem testes.** Nenhum unit test, nenhum integration test, nenhuma validação de invariantes.
14. **Sem seed centralizada / reprodutibilidade.** Cada agente tem seu próprio `std::mt19937` com seed derivada do id, mas o Engine usa `std::random_device`. Mesma simulação não reproduz.

---

## REQUISITOS — O QUE O SISTEMA REFATORADO DEVE SER

### A. NÚCLEO MATEMÁTICO E DE DADOS

**A1. Tipos numéricos discretos.**
- Preço em ticks inteiros (`int64_t price_ticks`) com `tick_size` configurável (ex: 0.01).
- Quantidade em lotes inteiros (`int64_t qty_lots`) com `lot_size`.
- Conversões para `double` apenas em fronteiras (UI, logs). Aritmética interna **inteira** — elimina drift de float em comparações de preço.

**A2. Tempo.**
- `tick` lógico (uint64_t) + `wall_time` (chrono nanoseconds) separados.
- `Clock` deve suportar 3 modos: `RealTime(secondsPerTick)`, `Accelerated(factor)`, `AsFastAsPossible`.
- Cada `Order`, `Trade`, `Cancel`, `Event` carrega `tick` E `seq_no` global monotônico (para tie-breaking determinístico).

**A3. RNG centralizado e reprodutível.**
- Uma `RngService` com seed única configurável. Cada consumidor (cada agente, cada gerador de eventos) recebe um `std::mt19937_64` derivado por hash(seed_global, consumer_id) — `seed_seq` ou splitmix.
- Toda simulação com mesma seed + mesma config = mesmo resultado bit-a-bit.

**A4. Conservação contábil estrita.**
- Invariante: `Σ posições(t) == oferta_total_emitida(t)` em todo tick, para todo ticker.
- Invariante: `Σ caixa(t) + Σ posições(t) * mid(t) - Σ caixa(0) - Σ posições(0) * preço(0) == 0` (fora fees).
- Permitir short selling **explicitamente** com `margin_requirement`, `borrow_fee`, e checagem de capacidade.
- Toda transferência (cash↔cash, asset↔asset) passa por um único módulo `Clearing` que emite `LedgerEvent` auditáveis.

### B. ORDER BOOK PROFISSIONAL

**B1. Estrutura de dados.**
- Dois `std::map<int64_t, PriceLevel>` (bid descending, ask ascending) onde `PriceLevel` é uma fila intrusive linked list de `OrderNode` com (id, agent_id, qty_remaining, seq_no, insert_tick, ttl).
- `unordered_map<order_id, list_iterator>` para cancel-by-id O(1).
- Suportar ordens: `Limit`, `Market`, `IOC`, `FOK`, `PostOnly`, `Cancel`, `Modify`.

**B2. Matching.**
- Price-time priority real. Preço de execução = preço do **maker** (resting). Maker rebate / Taker fee opcional via `FeeModel` plugável.
- Self-trade prevention configurável: `CancelOldest`, `CancelNewest`, `CancelBoth`, `Reject`.
- Partial fills com qty residual de volta na cabeça correta da fila.

**B3. Latência e eventos.**
- Toda ordem submetida vira `IncomingOrderEvent` com `arrival_tick` = `current_tick + agent_latency + jitter`. Não execute imediatamente — coloque na **event queue** ordenada e processe no tick correto.
- Modelo de latência por agente: `base_latency_ticks`, `jitter_distribution` (ex: lognormal). Permite simular HFT vs slow agents.

**B4. Cancelamento real.**
- Agentes podem cancelar ordens próprias por id. TTL é só fallback.
- Implemente `CancelEvent` na mesma queue de eventos com latência aplicada.

### C. MICROESTRUTURA REAL — O SNAPSHOT QUE OS AGENTES VÊEM

**C1. `MarketSnapshot` deve ser construído pelo Engine a cada tick, com TODOS os campos calculados de verdade:**
- `mid_price`, `micro_price` (size-weighted), `weighted_mid` (book-weighted N níveis).
- `spread`, `relative_spread`.
- `bid_depth_lN`, `ask_depth_lN` (top-N níveis separados, não só top-1).
- `book_imbalance` em N profundidades.
- `realized_volatility` em janelas (1m, 5m, 15m equivalentes em ticks) — calcular incrementalmente.
- `vwap` rolante por janela.
- `trade_imbalance` (volume buy-initiated vs sell-initiated, classificação Lee-Ready).
- `order_flow_imbalance` (Cont-Kukanov).
- `recent_returns`, `momentum`, `acceleration`.
- `time_since_last_trade` (estagnação real, não chute).
- `queue_position` se a ordem do agente é maker.

**C2. Eficiência:** snapshots não devem recopiar `priceHistories` inteiros. Use `span`/`view` ou janelas circulares pré-computadas. Toda métrica acima deve ter um `RollingWindow` incremental no Engine.

**C3. Heterogeneidade de informação.** Cada agente recebe um snapshot derivado com **ruído idiossincrático configurável** (preço percebido = real + N(0, σ_agent)) e possivelmente **delay informacional** (vê o book de N ticks atrás). Modela assimetria de informação.

### D. AGENTES — REFATORAÇÃO COMPLETA

**D1. Interface.**
- `IAgent::on_market_data(const MarketSnapshot&) -> std::vector<Action>` onde `Action` é variant<SubmitOrder, CancelOrder, Modify>.
- Estado interno do agente isolado (sem acesso ao Engine ou OrderBook diretamente).
- Cada agente declara seu `LatencyProfile`, `RiskLimits`, `InformationProfile`.

**D2. Família de agentes (recalibre todos contra a literatura):**
- **MarketMaker (Avellaneda-Stoikov):** quotes ótimas com inventory aversion γ, vol estimada σ, time-to-end T. Reservation price `r = s - q·γ·σ²·(T-t)`, optimal spread `δ = γ·σ²·(T-t) + (2/γ)·ln(1+γ/k)`. Skew por inventário.
- **Informed Trader (Kyle):** recebe sinal sobre fundamental value com vantagem temporal, escolhe tamanho ótimo proporcional a `λ⁻¹·sinal` (Kyle's lambda).
- **Noise Trader (Black):** Poisson arrival, side aleatório, tamanho lognormal. Calibrado para gerar volume de fundo realista.
- **Momentum/Trend Follower:** filtro técnico (MA cross, breakout, MACD) com risk management.
- **Mean Reverter:** Ornstein-Uhlenbeck thresholds, z-score entry/exit.
- **Value Investor:** compara preço vs fundamental percebido, posição alvo proporcional ao gap.
- **Liquidity Consumer / Institutional Execution:** TWAP, VWAP, POV (Percentage of Volume), Implementation Shortfall (Almgren-Chriss).
- **Stop-Loss Trigger Cluster:** simula liquidações em cascata em quedas (gera flash crashes).
- **News Reactor:** reage a `NewsEvent` com latência variável e magnitude proporcional ao impacto percebido (heterogêneo).

**D3. Risk management real por agente:**
- Hard limits: `max_position`, `max_notional`, `max_drawdown`, `max_leverage`.
- Margem: agentes com posição short pagam `borrow_fee` por tick.
- Liquidação forçada se equity < margin_requirement (gera evento de `MarginCall` que vira ordem de mercado agressiva).

**D4. Spawn dinâmico.** Substitua o `spawnRandomAgent` aleatório por uma `AgentFactory` com `PopulationConfig` declarativo (YAML/JSON ou struct C++) — quantos de cada tipo, faixas de parâmetros, distribuições.

### E. DRIVER ECONÔMICO — FUNDAMENTAL VALUE

**E1.** Cada ticker tem um `FundamentalValueProcess` (escolha plugável):
- GBM (geometric Brownian): `dS/S = μ dt + σ dW`.
- Jump-diffusion (Merton): GBM + Poisson jumps lognormais.
- Ornstein-Uhlenbeck (mean-reverting fundamental).
- Regime-switching (Hamilton): muda entre `low_vol` / `high_vol` / `crash` por cadeia de Markov.

**E2.** Agentes informados observam o fundamental com ruído. Agentes não-informados não veem nada do fundamental — só inferem via book.

**E3.** `NewsEventProcess` gera eventos exógenos (Poisson com intensidade λ) com magnitude amostrada de fat-tailed distribution. Substitui o `MarketEvent` atual.

### F. ARQUITETURA E THREADING

**F1.** Decomponha o `Engine` monolítico em:
- `MatchingEngine` (pure: events in, trades+book updates out).
- `MarketDataPublisher` (constrói snapshots, mantém rolling windows).
- `AgentRunner` (despacha snapshots para agentes, coleta actions).
- `RiskGate` (valida actions contra limits, rejeita ou ajusta).
- `Clearing` (aplica trades ao ledger, emite eventos contábeis).
- `EventBus` (pub-sub interno desacoplando módulos).
- `SimulationLoop` (orquestra a ordem do tick).

**F2. Threading correto.**
- Sim thread escreve estado.
- UI lê via `SnapshotBuffer` triple-buffered ou `seqlock` (lock-free read). Nunca acessa estruturas mutáveis do Engine diretamente.
- Logging em thread separada, alimentada por SPSC ring buffer (ex: `boost::lockfree::spsc_queue` ou implementação própria). Hot path **nunca** toca disco.

**F3. Determinismo.** Mesmo com threads, o resultado da simulação deve ser reprodutível — toda decisão depende apenas do estado da sim thread. Threads auxiliares são puro consumo.

### G. VISUALIZAÇÃO E OBSERVABILIDADE

**G1.** Reorganize a UI em painéis:
- **Order Book Heatmap** (depth ao longo do tempo).
- **Candles + Volume + VWAP** (já tem candles, melhore).
- **Trade Tape** (lista rolante de prints com taker side colorido).
- **Per-Agent Inspector** (PnL, posição, ordens ativas, decisões recentes).
- **Microstructure Dashboard** (spread, imbalance, OFI, realized vol — séries temporais).
- **Stylized Facts Panel** (histograma de retornos, ACF de retornos, ACF de |retornos|, Q-Q plot vs normal).
- **Regime Indicator** (qual regime do fundamental + indicadores derivados do book).

**G2.** Controles: pause/resume já existe — adicione step (1 tick), step (N ticks), velocidade, hot-reload de `PopulationConfig`, injeção manual de `NewsEvent`.

### H. LOGGING, PERSISTÊNCIA, REPLAY

**H1.** Schema unificado de eventos (`OrderSubmitted`, `OrderAccepted`, `OrderRejected`, `OrderCancelled`, `Trade`, `MarketDataSnapshot`, `LedgerEvent`, `NewsEvent`, `RegimeChange`).
**H2.** Escrita em formato binário compacto (ex: protobuf, flatbuffers, ou `cereal`) para o log principal, CSV apenas como export opcional.
**H3.** **Replay determinístico:** dado um log + seed + config, reproduzir bit-a-bit a simulação.

### I. VALIDAÇÃO — FATOS ESTILIZADOS DE MERCADOS REAIS

O simulador só está pronto quando, a partir de uma calibração razoável da população, **reproduzir empiricamente** os seguintes fatos estilizados (Cont 2001, Bouchaud, Lillo-Farmer):

1. **Retornos sem autocorrelação significativa** (ACF de `r_t` ≈ 0 para lag > 1).
2. **Volatility clustering:** ACF de `|r_t|` decai lentamente (long memory).
3. **Fat tails:** distribuição de retornos com kurtosis > 3, cauda em lei de potência (Hill estimator).
4. **Leverage effect:** correlação negativa entre `r_t` e `σ_{t+k}`.
5. **Volume-volatility correlation positiva.**
6. **Spread se abre em alta vol** (correlação spread × |retorno|).
7. **Sign autocorrelation positiva no order flow** (Lillo-Farmer): sequências de buy/sell são persistentes.
8. **Flash crashes esporádicos** quando liquidity providers recuam + stops disparam.

Crie um módulo `analysis/StylizedFacts.cpp` que computa essas métricas a partir do log e gera um relatório.

### J. TESTES

**J1.** Unit tests (Catch2 ou GoogleTest) para:
- `OrderBook` (price-time priority, partial fills, self-trade prevention, cancel, FOK/IOC).
- `PositionLedger` (conservação, short, fees, margin).
- `RollingWindow` (vol, vwap, ofi).
- `Clearing` (invariantes contábeis).
- `RngService` (reprodutibilidade).

**J2.** Integration tests:
- Cenário 1 agente MM + 1 noise trader → spread converge para Avellaneda-Stoikov teórico.
- Cenário liquidação forçada → trade de mercado é gerado e position vai a zero.
- Cenário replay determinístico → mesmo log gerado duas vezes.

**J3.** Property tests para invariantes contábeis e de book.

---

## ORDEM SUGERIDA DAS FASES (a `spec-plan` deve refinar)

1. **Fase 0 — Infra:** RngService, Clock multimodo, EventBus, tipos numéricos discretos, build com testes (Catch2).
2. **Fase 1 — Order Book profissional:** estruturas novas, matching correto, cancel/modify, latência via event queue. Testes.
3. **Fase 2 — Ledger e Clearing:** short selling, margin, fees, invariantes auditáveis. Testes.
4. **Fase 3 — Microestrutura real:** snapshots populados de verdade, rolling windows. Heterogeneidade de informação.
5. **Fase 4 — Driver econômico:** FundamentalValueProcess, NewsEventProcess, regime switching.
6. **Fase 5 — Refatoração de agentes:** todos recalibrados contra a literatura, com risk management real, latency profile, info profile. AgentFactory + PopulationConfig.
7. **Fase 6 — Threading correto:** decompor Engine, snapshot buffer triple-buffered/seqlock, logging em thread dedicada com ring buffer.
8. **Fase 7 — Visualização:** painéis novos (heatmap, tape, microstructure dashboard, stylized facts panel, agent inspector).
9. **Fase 8 — Persistência e Replay:** log binário, replay bit-a-bit determinístico.
10. **Fase 9 — Validação:** módulo `StylizedFacts`, calibração da população até reproduzir os 8 fatos. Iterar parâmetros até o mercado estar "vivo".
11. **Fase 10 — Documentação:** invocar `doc-generator`.

---

## DIRETRIZES DE EXECUÇÃO

- **Não preserve compatibilidade com o código atual.** Refatore com liberdade. O que existe é protótipo.
- **Não acumule dívida.** Cada fase deve ter testes verdes antes de avançar.
- **Justifique numericamente.** Toda fórmula nova citar a fonte (paper, livro, capítulo) em comentário breve no header relevante.
- **Mantenha o `main.cpp` mínimo** — só wiring entre módulos via DI.
- **Performance importa, mas não premature-optimize.** Meça. Profile com `perf`/Instruments antes de microtuning.
- **Fail loud, não fail silent.** Validações de invariantes devem `assert` em debug e logar `ERROR` em release. Nada de `if (!cond) return;` mudo como o código atual está cheio.
- **Comentários só onde o "porquê" não é óbvio.** Não comente o "o quê".
- **Sem `std::cout` no hot path.** Use o sistema de logging.

---

## ENTREGÁVEL FINAL

Quando terminar, eu devo conseguir:

1. Rodar `./simulador` e observar um mercado vivo, com volatilidade variável, eventos esporádicos, spread reagindo a estresse, agentes morrendo e nascendo.
2. Pausar, inspecionar agente individual, ver o book em profundidade real.
3. Disparar uma news event manualmente e ver o mercado reagir.
4. Rodar com `--seed 42` duas vezes e obter resultados idênticos.
5. Rodar `./analysis/stylized_facts log.bin` e ver os 8 fatos validados num relatório.
6. Ler `docs/` (gerado pela `doc-generator`) e entender a arquitetura completa em 30 minutos.

---

## COMECE AGORA

1. Leia o código atual completamente (todos os arquivos em [src/](src/)).
2. **Invoque a skill `spec-plan`** para produzir a especificação e o plano por fases conforme acima.
3. Apresente a spec/plan para revisão antes de implementar.
4. Execute fase por fase, validando cada uma com testes.
5. Ao final, **invoque a skill `doc-generator`**.
