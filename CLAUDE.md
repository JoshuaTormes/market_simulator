# simulacao_mercado_3

Simulador de microestrutura de mercado financeiro em **C++17** (CMake). Modela formação de preços
tick-a-tick com order book real, agentes heterogêneos e um processo fundamental latente. O preço é
**endógeno**: nasce do cruzamento de ordens, e nenhum componente escreve o preço à mão.

## Build e Testes

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build          # 29/29 testes
```

Alvos: `simulador` (UI), `sim_headless`, `sim_calibrate`, `analysis_csv_export`.
Sem SFML/ImGui: `cmake -S . -B build_noui -DSIM_BUILD_UI=OFF`.

## CLI

```bash
# Uma rodada + relatório de fatos estilizados inline
./build/sim_headless --seed 42 --duration 50000 --output logs/run.bin

# Ensemble de 10 seeds (é o ensemble que decide, não uma seed)
./build/sim_calibrate --seeds 10 --duration 50000 --outdir /tmp/cal/

# Teste de emergência: innovations gaussianas no fundamental
./build/sim_calibrate --seeds 10 --duration 50000 --gaussian

./build/analysis_csv_export logs/run.bin out/
```

Flags de `sim_headless`: `--seed --duration --output --ticker --process regime|gbm|ou|jump
--sigma-annual --news-per-day --gaussian`. Não há arquivo de configuração; os parâmetros vivem em
`src/core/Config.h` e o que é ajustável em runtime está nas flags.

## Branch Atual

`feature/endogenous-price-discovery` — revisão arquitetural concluída. Ensemble de 10 seeds × 50k:
price discovery corr(h=100) = 0,86 em 10/10 seeds, `std(log(mid/V))` = 0,18%, 6,6/8 fatos
estilizados (82,5%). Fato 2 e Fato 5 são falhas documentadas, ver
[docs/STYLIZED_FACTS_VALIDATION.md](./docs/STYLIZED_FACTS_VALIDATION.md).

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
- Log binário via `BinaryLogWriter/Reader`, schema v3 (`EventSchema.h`); o v3 carrega
  `fundamental_value` no snapshot, que é o que permite medir price discovery offline.

## Parâmetros Críticos (não alterar sem re-rodar o ensemble de 10 seeds)

| Onde | Parâmetro | Por quê |
|---|---|---|
| `FundamentalConfig` | `sigma_annual = 0.30`, `mu_annual = 0.0` | uma única alavanca de vol, martingale; regimes são multiplicadores {1; 2,5; 6} e não níveis absolutos |
| `MarketMakerAS.cpp` | TTL = `snap.base.tick + 3` | agentes veem `prev_snap_` (tick `now-1`) e `expire(now)` roda antes de `publish`; com TTL menor a cotação morre antes de ser vista |
| `MarketMakerParams` | `mm_qty = 200`, `q_soft = 600` | a profundidade cotada tem que passar de uma ordem agressiva típica (filho institucional vai até 200), senão uma ordem de mercado esvazia o book |
| `MarketMakerParams` | `lambda_kyle = 0.06` | compromisso: subir melhora seleção adversa e piora a ACF de retorno; zerar mata o clustering de volatilidade e o price discovery |
| `InformedTraderParams` | `q_soft_frac = 0.6` | o limite duro é do broker; o orçamento do próprio trader é menor. Agente encostado no limite parou de descobrir preço |
| `NoiseTraderParams` | `q_scale = 500` | tilt fraco contra o inventário. Sem ele a coorte anda até o limite de posição e o fluxo rejeitado vira empurrão direcional espúrio |
| `InstitutionalParams` | `arrival_lambda = 0.002`, `q_scale = 1500` | parents continuam chegando (um a cada ~500 ticks) e o lado é sorteado contra o inventário do desk |
| `StopLossParams` | `trigger_range = {0.015, 0.04}`, `cooldown_range = {200, 800}` | com o preço colado no fundamental (~0,2%) a banda antiga de 4–8% nunca era atingida; o cooldown é o que faz o cluster re-armar em vez de morrer no primeiro stop |
| `NewsReactorParams` | `impact_scale = 0.004`, igual a `NewsConfig::impact_scale` | é a referência contra a qual `base_qty` é cotado; fora de sincronia toda reação colapsa no piso de 1 lote. O lag curto e o disparo único por evento (em tick sorteado dentro da janela) são o que espalha o impacto pela coorte em vez de repetir ordem por agente |
| `MomentumParams` / `MeanReverterParams` | `count = 3` / `count = 2` | o par de estratégias opostas é o que segura a ACF de retorno; momentum sem contraparte é só manada |
| `Report.cpp` | Fato 2 nos lags 1 e 2 (< 0,05), Fato 3 no lag 1 (> 0,05) | thresholds são a definição do fato e não se mexe neles para passar |
