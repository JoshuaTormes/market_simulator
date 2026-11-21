# simulacao_mercado_3

Simulador de microestrutura de mercado financeiro em **C++17** (CMake). Modela formação de preços
tick-a-tick com order book real, agentes heterogêneos e um processo fundamental latente. O preço é
**endógeno**: nasce do cruzamento de ordens, e nenhum componente escreve o preço à mão.

## Build e Testes

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build          # 31/31 testes
```

Alvos: `simulador` (UI), `sim_headless`, `sim_calibrate`, `analysis_csv_export`.
Sem SFML/ImGui: `cmake -S . -B build_noui -DSIM_BUILD_UI=OFF`.
A UI (`simulador`) só linka com o clang da Apple, porque o `sfml@2` do Homebrew usa libc++:
`cmake -S . -B build_ui -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang`
e `./build_ui/simulador`. A mesma seed dá outra trajetória no clang (as distribuições da libc++ e a
libm diferem), então os números de referência valem só para o build g++ em `build/`.

## CLI

```bash
# Uma rodada + relatório de fatos estilizados inline
./build/sim_headless --seed 42 --days 5 --output logs/run.bin

# Calibração: seeds CAL 1–10, 60 pregões, 8 threads, pareado contra uma referência
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir /tmp/cal \
    --baseline specs/plans/2026-11-18_correcao-circularidade-validacao/resultados/r11_cal.csv

# Validação hold-out: seeds VAL 101–120, uma vez por decisão
./build/sim_calibrate --seed-set val --days 60 --jobs 8 --outdir /tmp/val

# Teste de emergência: innovations gaussianas no fundamental
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --gaussian --outdir /tmp/gauss

./build/analysis_csv_export logs/run.bin out/
```

Flags de `sim_headless`: `--seed --duration --days --output --ticker --process regime|gbm|ou|jump
--sigma-annual --news-per-day --gaussian --news-noise S --news-blind --taker-fee BP --maker-rebate BP`.
`sim_calibrate` aceita as mesmas de modelo e mais `--seed-set cal|val --seeds --seed-start --jobs
--outdir --csv --baseline --keep-logs --save --set chave=valor` (ablações; lista em `apply_set`).
Os dois saem com código 2 em argumento desconhecido ou flag sem valor. No zsh, uma variável com
várias flags chega como **um** argumento; escreva as flags literalmente e confira a linha
`[INFO] seeds=… ticks=…` antes de ler um ensemble. Não há arquivo de configuração; os parâmetros
vivem em `src/core/Config.h`.

## Branch Atual

`feature/validacao-honesta` — execução do plano em
[specs/plans/2026-11-18_correcao-circularidade-validacao/](./specs/plans/2026-11-18_correcao-circularidade-validacao/plano.md).
Etapas 0–13 e a Verificação Final concluídas (VAL × 250 pregões em `resultados/final_val250`). Commits `4360504` (plano) e `99c4feb`
(reconciliação de momentum e mean-reverter); `specs/` fica fora do git.

**Protocolo.** As métricas são separadas em CAL (podem orientar ajuste), VAL (hold-out: julgam uma
mudança já escolhida e **nunca** orientam ajuste) e diag (`src/analysis/Protocol.h`). CAL = seeds
1–10, VAL = seeds 101–120, 60 pregões. Uma mudança só melhora ou piora com |Δ pareado| > 2·SE. A
bateria legada (8 fatos a 1 s) virou diagnóstico; a bateria v2 (10 fatos em retornos de calendário,
com IC por bootstrap) é a que vale.

**Referência atual** (`resultados/r11_cal`, `r11_val`; média ± SE entre seeds):

| medida | CAL | VAL |
|---|---|---|
| bateria v2, fatos PASSA de 10 | 5,4 ± 0,16 | 5,1 ± 0,12 |
| bateria legada (diag), de 8 | 6,0 | 6,05 ± 0,20 |
| δ de metaordem (alvo 0,4–0,7) | 0,72 ± 0,05 | 0,73 ± 0,04 |
| corr(r_mid, r_difusão de V), h=100 | 0,49 ± 0,01 | 0,47 ± 0,01 |
| corr(r_mid, r_V), h=100 (diag) | 0,80 ± 0,01 | 0,82 ± 0,01 |
| meia-vida do gap mid/V (alvo < 50) | 281 ± 13 ticks | 271 ± 12 ticks |
| vol anual do mid / de V (1 min) | 0,334 / 0,287 | 0,347 / 0,306 |
| spread, razão U, maior maker | 2,68 bp, 1,58, 0,40 | 2,71 bp, 1,57, 0,39 |
| ACF de sinal de trade (alvo 0,10–0,40) | 0,142 | 0,145 |
| P&L US$ mil: makers / informados / LOT / noise | 122 / 347 / 61 / −368 | 126 / 324 / 70 / −361 |

Soma zero exata em todas as seeds. Informados e LOTs ficam positivos em 10/10 e 20/20 seeds; os makers
em 10/10 e 19/20. Nenhuma seed colapsa, nem em 60 nem em 250 pregões. Na VAL × 250 as taxas por pregão (P&L, flash
crashes) ficam constantes, e spread, concentração, price discovery e a bateria v2 (5,1) não andam. O
δ de metaordem cai para 0,50 ± 0,03 (16/20), só por haver mais parents no ajuste por faixas: δ só se
compara no mesmo horizonte.
Com innovations gaussianas (`resultados/e13_gauss`) as caudas a 1 s e a 5 min ficam intactas e nada
muda acima de 2·SE em CAL+VAL: elas não vêm da cauda das innovations da difusão.

**Falhas medidas, não escondidas:**
- v2.3: o retorno a 1 min reverte (ρ ≈ −0,15).
- v2.5: memória longa da vol, 0/10 e 2/20 seeds.
- v2.8: o efeito alavancagem está ausente (corr ≈ 0).
- v2.10: a memória do sinal de trade decai rápido demais (3 seeds).
- Fatos legados 2 e 3 falham: |ACF r| = 0,058 e ACF|r| = 0,038.
- O gap fecha devagar, o volume maker contra maker é 14% e há ~18 flash crashes por rodada.

**Números antigos que não valem mais.**
- "7,0/8 fatos": o Fato legado 3 que passava era a grade da cadência fixa dos makers.
- "corr 0,77": é majoritariamente notícia, que o maker lê de graça pelo EventBus. A parte difusiva de
  V é incorporada pela metade em 100 s.
- "Expoente de impacto 0,51": era a regra côncava do maker lida de volta. A medida é o δ de metaordem
  (0,72 côncavo contra 0,89 com a regra linear).

## Documentação Detalhada

- [docs/ARCHITECTURE_OVERVIEW.md](./docs/ARCHITECTURE_OVERVIEW.md) — camadas, tick order, agentes, escala de tempo
- [docs/STYLIZED_FACTS_VALIDATION.md](./docs/STYLIZED_FACTS_VALIDATION.md) — os 8 fatos, price discovery, falhas medidas

## Convenções

- Todos os agentes herdam `AgentBase`. Nova estratégia: criar `src/agents/MeuAgente.{h,cpp}`,
  adicionar params em `Config.h` e registrar em `AgentFactory.cpp`.
- **Determinismo**: nenhum `std::random_device`; toda aleatoriedade via `RngService::for_consumer(id)`.
  Sorteios que só acontecem sob condição de estado tornam o stream dependente do estado — sortear
  sempre e usar (ou descartar) o valor depois.
- **Tick order** em `SimulationLoop::tick_once` é fixo e documentado no código — não alterar a sequência.
- Preços são `int64_t` em tick-units (10000 = $100.00 com `tick_size = 0.01`).
- **Escala de tempo única**: `TimeScale` (1 tick = 1 s, 23400 s/sessão, 252 dias). Toda grandeza com
  dimensão temporal é derivada dela — `sigma_annual = 0,30` → σ_tick ≈ 1,24e-4. Nunca fixar um σ
  por tick na mão.
- Log binário via `BinaryLogWriter/Reader`, schema v4 (`EventSchema.h`). O snapshot carrega
  `fundamental_value` e `news_cum_log`, o que separa difusão e notícia no price discovery offline, e
  há um registro `Metaorder` por parent institucional concluído. O reader lê logs v2 e v3.
  `sim_calibrate` não grava log por padrão: coleta as séries em memória (`--keep-logs` grava).
- **Taxas** em bp do nocional, contabilizadas em unidades de 1/1000 de tick (`kFeeUnitsPerTick`) em
  `Trade`, no ledger e no log. Isso mantém a soma exata em inteiros: 0,1 bp a $100 é 0,1 tick por lote.

## Parâmetros Críticos (não alterar sem re-rodar o ensemble de 10 seeds)

| Onde | Parâmetro | Por quê |
|---|---|---|
| `FundamentalConfig` | `sigma_annual = 0.30`, `mu_annual = 0.0` | uma única alavanca de vol, martingale; regimes são multiplicadores {1; 2,5; 6} e não níveis absolutos |
| `MarketMakerAS.cpp` | TTL = `snap.base.tick + requote_period + 2` | agentes veem `prev_snap_` (tick `now-1`) e `expire(now)` roda antes de `publish`; com TTL menor a cotação morre antes de ser vista, e ela precisa sobreviver ao próprio ciclo de recotação do maker |
| `MarketMakerParams` | `requote_period_max = 3`, `random_cadence = true`, `accumulate_ofi = true` | maker `i` recota em intervalo médio `1 + (i % 3)`, **sorteado** uniforme em {1..2p−1}. Cadência fixa — mesmo escalonada em 1/2/3 — grava uma grade de período 6 no mid: ACF(\|r\|) 0,13 / 0,23 / 0,14 nos lags 1-3 e 0,21 no lag 6, P(mid anda) de 0,55 a 0,89 por fase do tick. O Fato legado 3 que passava (0,107) era essa grade; sem ela o clustering a 1 s é ≈ 0,01 e o fato falha honestamente (v2.4, a 1 min, passa). O acúmulo faz o maker lento precificar todo o fluxo desde a última recotação; sem ele o maker de período 1 era o único a absorver o fluxo e perdia ~US$ 300 mil por 60 pregões |
| `NewsConfig` / `FundamentalConfig` | `impact_scale = 0.00273`, `news_var_per_tick` | orçamento de variância (Merton 1976): `sigma_annual` é a vol do **processo de valor**, não de um componente. As notícias entram no mesmo `log V` via `apply_shock`, então a variância delas é subtraída e a difusão fica com o resto. A 0,4% o fluxo de notícias sozinho carregava 108% de um orçamento de 30% e o fundamental realizava 43%. As duas cópias de `impact_scale` (news e reactor) continuam tendo que ser iguais |
| `MarketMakerParams` | `impact_q_ref = 50.0` | impacto côncavo: a crença anda `λ·sign(OFI)·sqrt(\|OFI\|·q_ref)`. A regra linear de Kyle é o equilíbrio de um tiro só; fluxo agregado de ordens fatiadas é côncavo. O expoente **por tick** (≈ 0,58) é a regra lida de volta e não prova nada; a medida é o δ de metaordem, 0,72 ± 0,05 contra 0,89 com a regra linear (Etapa 8). O maker é *inspirado* em Avellaneda-Stoikov, sem γ nem horizonte, e o λ é fixo: não é Glosten-Milgrom |
| `AgentRunner.cpp` | permutação aleatória da ordem dos agentes por tick | ordem fixa + prioridade temporal dava a um único maker 72–93% do volume. Não é identidade de parâmetros, é a ordem de chegada. Desligar a permutação também **piora** o Fato 3 (0,013 vs 0,024) |
| `InformedTraderParams` | `speed = {0.015, 0.030}`, `target_lots_range = {40, 80}`, `deadband_sigma = 1.0` | o informado é negociador de **posição-alvo**, não de fluxo. A regra de fluxo antiga girava a posição e fazia dele o maior perdedor da mesa |
| `MarketMakerParams` | `half_spread_min_range = {0.3, 1.1}`, `vol_mult_range = {0.25, 0.55}` | calibrado com o spread em ~3,3 bp; com λ = 0,045 e os LOTs o spread é 2,7 bp. A 11–12 bp o excedente inteiro ia para os makers e o informado ficava negativo; abaixo disso o maker é que fica. Alargar o feedback de spread (`vol_mult` 0,55–1,10, `adverse_sel` 0,006) foi medido: não move o Fato 3 e devolve o excedente ao maker |
| `PopulationConfig` | `intraday = {floor 0.6, peak 1.8}` | forma U de Admati-Pfleiderer na taxa de chegada de noise, institucional e LOT; razão U medida 1,58, alvo > 1,30 |
| `StopLossParams` | `long_bias = 0.7` | assimetria de stops: a coorte é estruturalmente comprada, então uma queda aciona mais stops do que uma alta. **Não** é o efeito alavancagem de Black (1976), que vem do balanço da empresa; o v2.8 mede corr ≈ 0, o efeito está ausente no modelo |
| `FundamentalConfig` | `regime_duration_sec = {7200, 1800, 180}` | estados de volatilidade (calmo, elevado, alta vol ×6, todos com drift zero; o terceiro não é "crash") persistem por horas (Andersen & Bollerslev 1997), não por dezenas de segundos. A matriz de transição é derivada destas durações e do `TimeScale` |
| `MarketMakerParams` | `mm_qty = 200`, `q_soft = 600` | a profundidade cotada tem que passar de uma ordem agressiva típica (filho institucional vai até 200), senão uma ordem de mercado esvazia o book |
| `MarketMakerParams` | `lambda_kyle = 0.045` | recalibrado na CAL depois do acúmulo de OFI (com 0,06 a vol do mid ia a 0,375 contra 0,287 de V). Compromisso: 0,035 aproxima a vol de V mas tira a ACF de sinal do alvo e corta o P&L dos informados; subir piora a vol excedente; zerar mata o price discovery |
| `InformedTraderParams` | `q_soft_frac = 0.6` | o limite duro é do broker; o orçamento do próprio trader é menor. Agente encostado no limite parou de descobrir preço |
| `NoiseTraderParams` | `q_scale = 500` | tilt fraco contra o inventário. Sem ele a coorte anda até o limite de posição e o fluxo rejeitado vira empurrão direcional espúrio |
| `InstitutionalParams` | `arrival_lambda = 0.002`, `q_scale = 1500` | parents continuam chegando (um a cada ~500 ticks) e o lado é sorteado contra o inventário do desk |
| `StopLossParams` | `trigger_range = {0.015, 0.04}`, `cooldown_range = {200, 800}` | com o preço colado no fundamental (~0,2%) a banda antiga de 4–8% nunca era atingida; o cooldown é o que faz o cluster re-armar em vez de morrer no primeiro stop |
| `NewsReactorParams` | `impact_scale = 0.00273`, igual a `NewsConfig::impact_scale` | é a referência contra a qual `base_qty` é cotado; fora de sincronia toda reação colapsa no piso de 1 lote. O lag curto e o disparo único por evento (em tick sorteado dentro da janela) são o que espalha o impacto pela coorte em vez de repetir ordem por agente |
| `NewsReactorParams` | `hold_range = {300, 3600}` | horizonte de holding log-uniforme por reação. O maker já precifica a manchete pelo EventBus, então a reação é aposta de curto prazo; mantida para sempre, o inventário da coorte andou até ≈ −7000 lotes em 60 pregões, saturou os informados e soltou o gap (26% → 0,28% com o horizonte) |
| `MomentumTrader.cpp` / `MeanReverterOU.cpp` | ordem = alvo − `snap.own_inventory` | o flag interno `position_` contava ordem perdida como executada; o inventário real do momentum ia a ≈ 500 lotes com `qty = 10` e ele ficava 70–89% dos ticks no limite. Nunca medir posição fora do ledger |
| `PopulationConfig` | `price_ref_ticks = 10000` | parâmetros em ticks de maker e informado são lidos a esse preço e escalam com o mid. Em ticks absolutos uma queda alargava o spread relativo e o passo da crença até `bid <= 0`. Não muda nada perto de $100; é o que vale para a validação de 250 pregões |
| `MomentumParams` / `MeanReverterParams` | `count = 3` / `count = 2` | o momentum é um seguidor de tendência intradiário (janelas de segundos), não o momentum de Jegadeesh-Titman. A nota antiga de que o par "segura a ACF de retorno" **não se sustenta**. Ela mudava `count` e com isso o stream de todos os agentes. Silenciados com `mom.qty=0`/`mr.qty=0`, com os sorteios intactos, o Fato 2 legado melhora (0,061 → 0,049) e nenhuma métrica piora (Etapa 8). Ficam como fluxo pagador: o momentum é o maior pagador do maker |
| `RiskLimits` | `max_drawdown_pct = 1.0` (limite de perda desligado na ecologia) | o padrão 0,20 era uma barreira absorvente sobre o P&L acumulado: o maker de período 1 cruzava o piso por volta do pregão 40 e saía para sempre, e o mercado seguia com 2 makers. Quem ligar o limite consegue zerar, porque ordem que só reduz \|posição\| nunca é bloqueada |
| `MarketMakerParams` | `news_signal_noise = 0`, `news_blind = false` | robustez da leitura da notícia pelo maker (`--news-noise`, `--news-blind`). Erro de leitura até s = 1 não derruba a corr com as notícias (0,655 → 0,647), porque os outros corrigem em poucos ticks; s > 0 só cria um transiente de clustering depois de cada notícia. O maker cego derruba essa corr para 0,36 e a bateria v2 para 3,4 |
| `LimitOrderTraderParams` | `count = 20` | traders de ordem limitada (Chiarella-Iori), que nunca cruzam o topo. Dão 1,7% da quantidade no topo, estreitam o spread em 3% e levam a ACF de sinal de 0,101 para 0,142, para dentro do alvo. A chegada ×3 (topo 5,6%) foi medida e não adotada: a corr com V piora. `lot.count=0` reproduz a referência anterior bit a bit, porque os LOTs são construídos por último |
| `SimulationConfig` | `taker_fee_bp = 0`, `maker_rebate_bp = 0` | nenhum agente vê a taxa, então ela só redistribui o P&L para a bolsa. Com 0,1 bp de taker, informados e makers seguem positivos em 10/10 seeds; os informados só zerariam com ~2,1 bp |
| `Report.cpp` | Fato 2 nos lags 1 e 2 (< 0,05), Fato 3 no lag 1 (> 0,05) | thresholds são a definição do fato e não se mexe neles para passar. Do mesmo jeito, **a VAL nunca orienta ajuste**: cada escolha cita a métrica CAL que a motivou, e a VAL roda uma vez por decisão |
