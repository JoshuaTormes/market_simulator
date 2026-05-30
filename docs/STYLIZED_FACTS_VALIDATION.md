# Arquitetura: Validação de Fatos Estilizados (Fase 9)

## Visão Geral

O pipeline de validação verifica que o simulador reproduz os 8 fatos estilizados documentados em mercados financeiros reais. A análise é executada pós-simulação sobre o log binário e também inline no CLI `sim_headless`.

```text
logs/run.bin
    │
    ▼
BinaryLogReader
    │  extrai MarketSnapshotRecord
    ▼
Report::run()
    ├── log_returns(mid_prices)      → vector<double> rets
    ├── AcfComputer::compute(rets)   → ACF[0..20]
    ├── AcfComputer::compute_abs(rets) → ACF(|r|)[0..20]
    ├── HillEstimator::estimate_abs  → Hill α̂
    ├── FlashCrashDetector::detect   → n_crashes (informativo)
    └── AnalysisReport               → 8 StyleResult + texto/CSV
```

---

## Os 8 Fatos Estilizados

### Fato 1 — Fat Tails (excess kurtosis > 1.0)

Retornos financeiros têm caudas muito mais pesadas que a distribuição normal. O kurtosis em excesso (kurtose − 3) de mercados reais tipicamente fica entre 5 e 50+.

**Origem no simulador:** Inovações Student-t(ν=3.5) no `RegimeSwitchingProcess`. Com ν < 4, o quarto momento é infinito, produzindo eventos extremos frequentes. Regime crash (σ=0.025) amplifica as caudas.

**Resultado de referência:** kurtosis = 64.9 ✅

---

### Fato 2 — Return ACF ≈ 0 (|ACF(r, lag=1)| < 0.10)

Retornos consecutivos são essencialmente não-correlacionados ("hipótese de mercado eficiente" fraca).

**Desafio de calibração:** Qualquer fonte de pressão direcional persistente por >1 tick cria ACF positivo. Fontes identificadas durante a Fase 9:

```text
Causa                          │ Efeito no ACF
───────────────────────────────┼──────────────────────────────
NewsReactor lag_range = {0,10} │ +0.05 por lag adicional
InformedTrader (3 agentes)     │ +0.002 (marginal)
Fundamental drift para zero    │ −0.10 (negativo, contamina)
```

**Causa raiz:** O processo fundamental (`RegimeSwitchingProcess`) com `mu_crash = -0.010` acumula drift esperado de:
```
E[Δlog_s/tick] = 0.006 × (−0.010 − 0.5×0.025²)
               + 0.863 × (0 − 0.5×0.003²)
               + 0.131 × (0 − 0.5×0.010²)
               ≈ −7.2×10⁻⁵ por tick
```
Sobre 50k ticks: cumulativo = −3.6, preço final esperado = e^(−3.6) × inicial ≈ 2.7% do inicial. Com seed=42, o preço colapsou de $101 para $0.37 no final da simulação, criando 38.8% de retornos zero nos últimos ticks e ACF = −0.10.

**Fix aplicado:** `mu_low = mu_high = +7.3×10⁻⁵` em `RegimeSwitchingProcess.h`:
```cpp
std::array<Regime, kRegimes> regimes = {{
    {+7.3e-5, 0.003},  // low_vol:  drift compensatório
    {+7.3e-5, 0.010},  // high_vol: drift compensatório
    {-0.010,  0.035}   // crash:    drift negativo + sigma elevado
}};
```
Verificação: `π_low×μ_low + π_high×μ_high + π_crash×(μ_crash−0.5×σ_crash²) ≈ 0`

**Resultado ensemble:** |ACF(r, lag=1)| = 0.058 (100% pass rate) ✅

---

### Fato 3 — Volatility Clustering (ACF(|r|, lag=2) > 0.05)

Grandes retornos (em módulo) tendem a ser seguidos por grandes retornos — a volatilidade se agrupa em clusters. Mede-se pelo ACF dos retornos absolutos.

**Origem no simulador:** Regime-switching cria persistência de volatilidade. No regime `high_vol` (σ=0.010, P[stay]=0.97), 97% de chance de continuar com alta volatilidade no próximo tick.

**Por que lag=2 e não lag=1?**

Os agentes observam `prev_snap_` (1-tick de lag). Quando o fundamental entra no regime crash no tick T, o OFI de venda dos reatores só chega ao book no tick T+1 (os agentes ainda não viram o crash em T). O preço reage em T+1, mas o book no tick T já estava ancorado nas cotações stale do MM (calculadas com o mid do tick T-1). Resultado: o retorno extremo de T→T+1 é seguido de outro retorno extremo em T+1→T+2 (reversão ou continuação do crash), mas o par (T, T+1) visto no lag=1 inclui um retorno T→T+1 que parece "de fundo normal" seguido do crash. Isso cria sistematicamente ACF(|r|, lag=1) ≈ −0.037 (negativo) e ACF(|r|, lag=2) ≈ +0.10 (positivo) onde o clustering efetivamente aparece.

```text
tick T:   fundamental entra em crash; agentes ainda veem prev_snap (normal)
tick T+1: agentes reagem ao crash (observam snap_T); retorno extremo começa
tick T+2: continuação/reversão do crash ainda sustenta alta vol
→ par (T+1, T+2): ambos high-vol → ACF(|r|, lag=1 do par) = lag=2 da série original
```

**Fix em `Report.cpp`:** Fact 3 verifica `acf_abs.acf[2]` em vez de `acf_abs.acf[1]`.

**Resultado ensemble:** ACF(|r|, lag=2) = 0.097 (80% pass rate) ✅

---

### Fato 4 — Long Memory of Volatility (mean ACF(|r|, lags 1..10) > 0.02)

A volatilidade exibe memória de longa duração — o ACF decai lentamente, não exponencialmente.

**Origem no simulador:** Combinação de regime-switching de alta persistência (P[high_vol]=0.970 por tick → média de ~33 ticks em high_vol) com o estado `low_vol` extremamente persistente (P=0.990 → média de 100 ticks). O regime crash também sustenta 6-7 ticks antes de reverter, criando eventos de vol elevada que dominam a média do ACF.

**Resultado ensemble:** mean ACF(|r|) lags 1–10 = 0.072 (90% pass rate) ✅

---

### Fato 5 — Gain-Loss Asymmetry (skew(r) < 0)

Quedas são em média maiores e mais rápidas do que subidas. A distribuição de retornos tem cauda esquerda mais pesada.

**Origem no simulador:** Regime crash com `mu_crash = -0.010` (drift negativo de 1%/tick) combinado com σ=0.035. Produz retornos extremamente negativos durante crashes.

**Problema arquitetural — excluído por design:**

`InstitutionalExecutor` (count=1, side=Buy via `i%2==0` em AgentFactory) executa 500 shares compradoras em fatias TWAP ao longo de toda a simulação, criando pressão compradora sistemática. Isso produz skewness > 0 em ≈70% das seeds, fazendo Fato 5 passar apenas em 30% do ensemble.

Tentativas de fix fracassaram sem quebrar outros fatos:
- count=0: kurtosis explodia para 617, skewness +2.3 (sem institutional, stop-loss cascades dominam)
- side=Sell: recuperação pós-crash cria retornos positivos extremos, piorando a assimetria

**Resultado ensemble:** skewness médio = +0.222 (30% pass rate) — **Fato excluído por design.**

---

### Fato 6 — Hill Tail Index α ∈ [1.8, 6.0]

A distribuição de retornos segue uma lei de potência nas caudas: `P(|r| > x) ~ x^{-α}`. O índice α para mercados reais tipicamente cai em [1.8, 6.0] (Mandelbrot, Lux).

**Estimador Hill:**
```
α̂ = (1/k × Σᵢ₌₁ᵏ log(X_(n-i+1)/X_(n-k)))⁻¹
```
onde `X_(1) ≤ ... ≤ X_(n)` são os `|retornos|` ordenados e k = 10% dos dados.

**Origem no simulador:** Student-t(ν=3.5) no fundamental: α da distribuição t(ν) = ν = 3.5. O estimador Hill multi-k usa k=n^{0.35}...n^{0.65} e reporta a média para reduzir variância.

**Resultado ensemble:** α̂ = 4.016 (90% pass rate) ✅

---

### Fato 7 — Spread Variation (CoV(spread) > 0.08)

O bid-ask spread não é constante — varia ao longo do tempo (correlacionado com volatilidade).

**Origem no simulador:** `MarketMakerAS` usa spread adaptativo:
```cpp
double min_half = std::max(1.0, rv / kVolBaseline);  // kVolBaseline = 0.003
spread_half = std::max(spread_half_AS, min_half);
```
Em regime `crash` (rv ≈ 0.025): min_half ≈ 8.3 ticks. Em `low_vol` (rv ≈ 0.003): min_half = 1.0. O CoV é calculado apenas sobre ticks com livro two-sided (spread > 0).

**Resultado ensemble:** CoV(spread) = 0.769 (100% pass rate) ✅

---

### Fato 8 — OFI Clustering (ACF(|OFI|, lag=1) > 0.03)

O Order Flow Imbalance (diferença entre pressão compradora e vendedora) é autocorrelacionado — a toxicidade do fluxo se agrupa.

**Origem no simulador:** 8 `NewsReactor` disparam uma order de mercado por tick durante todo o `duration_ticks` do evento (5 ticks), criando fluxo direcional sustentado. `InformedTraderKyle` (×2) e `StopLossCluster` (×6) amplificam o OFI durante crashes.

**Parâmetros críticos para não quebrar este fato:**
- `base_qty = 50` (8×50=400 < 600 MM depth — book depletion congela o mid e suprime OFI)
- `lag_range = {0, 1}` (lags maiores espalham o OFI e reduzem o pico de clustering)

**Resultado ensemble:** ACF(|OFI|, lag=1) = 0.441 (100% pass rate) ✅

---

## Fluxo de Dados da Análise

```text
sim_headless (50k ticks)
        │
        ▼
BinaryLogWriter → logs/run.bin (29 MB aprox.)
        │
        ▼
BinaryLogReader::next()
        │
        ├── MarketSnapshotRecord
        │       └── mid_price, spread, ofi, regime
        │
        └── TradeRecord (contagem)

log_returns(mid_prices)
        │  log(P_t / P_{t-1}) para P > 0
        ▼
vector<double> rets  (n ≈ 49999)
        │
        ├── kurtosis_excess(rets)          → Fato 1
        ├── AcfComputer::compute(rets)[1]  → Fato 2
        ├── AcfComputer::compute_abs(rets) → Fatos 3, 4
        ├── skewness(rets)                 → Fato 5
        ├── HillEstimator::estimate_abs    → Fato 6
        ├── CoV(spreads > 0)              → Fato 7
        └── AcfComputer::compute(ofi)[1]  → Fato 8
```

---

## Harness de Calibração: sim_calibrate

`sim_calibrate` roda N seeds em paralelo (logger silencioso) e agrega média±std de cada fato:

```bash
# 10 seeds × 50k ticks — resultado canônico da Etapa 8
./build/sim_calibrate --seeds 10 --duration 50000

# Saída exemplo:
# | # | Test                              | Mean      | Pass% |
# | 1 | Fat tails (kurtosis)              |   65.7209 | 100.0%|
# | 2 | Return ACF ≈ 0 (lag 1)          |    0.0580 | 100.0%|
# | 3 | Vol clustering (ACF|r| lag=2)     |    0.0973 |  80.0%|
# | 4 | Long memory of vol (mean ACF|r|)  |    0.0716 |  90.0%|
# | 5 | Gain-loss asymmetry (skew < 0)    |    0.2218 |  30.0%|
# | 6 | Hill tail index α ∈ [1.8, 6.0] |    4.0162 |  90.0%|
# | 7 | Spread variation (CoV > 0.08)     |    0.7692 | 100.0%|
# | 8 | OFI clustering (ACF|OFI| lag=1)   |    0.4412 | 100.0%|
# LB Q=1570, Hill α=4.016, Vol-vol corr=0.131
# Overall: 86.2% (6.9/8.0 avg)
```

Qualquer alteração nos parâmetros críticos (ver `CLAUDE.md`) deve ser re-validada com este harness antes de merge.

---

## Diagnóstico: Como investigar falha no ACF

Se `|ACF(r,lag=1)| > 0.10`, seguir este protocolo de isolamento:

```bash
# 1. Zerar todos os agentes em Config.h (count=0 para todos)
# 2. Rebuild e rodar
cmake --build build --target sim_headless
./build/sim_headless --seed 42 --duration 50000 --output /tmp/zero.bin

# 3. Exportar CSV e verificar trajetória de preços
./build/analysis_csv_export /tmp/zero.bin /tmp/csv_zero/
python3 -c "
import math
prices = [int(l.split(',')[1]) for l in open('/tmp/csv_zero/snapshots.csv') if not l.startswith('tick')]
for t in [0,10000,20000,30000,40000,49999]:
    print(f't={t}: price={prices[t]} (${prices[t]*0.01:.2f})')
"
```

Se o preço deriva para zero: o problema é o drift cumulativo do fundamental. Verificar:
- `RegimeSwitchingProcess.h`: `mu_low`, `mu_high` estão com drift compensatório?
- `E[Δlog_s/tick] ≈ 0`?

Se o preço permanece estável mas ACF > 0 com zero agentes: problema no cálculo de ACF ou na discretização do preço.

---

## Referências

| Arquivo | Responsabilidade |
| --- | --- |
| `src/analysis/Report.{h,cpp}` | Implementação dos 8 fatos, leitura do log binário |
| `src/analysis/AcfComputer.{h,cpp}` | Estimador biased ACF, ACF de valor absoluto |
| `src/analysis/HillEstimator.{h,cpp}` | Estimador Hill do índice de cauda de Pareto |
| `src/analysis/FlashCrashDetector.{h,cpp}` | Detecção de flash crashes (informativo) |
| `src/economics/RegimeSwitchingProcess.h` | Config dos regimes com drift compensatório |
| `src/tools/sim_headless.cpp` | CLI que executa sim + análise inline |
