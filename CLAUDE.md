# simulacao_mercado_3

Simulador de microestrutura de mercado financeiro em **C++17** (CMake). Modela formação de preços tick-a-tick com order book real, múltiplos agentes heterogêneos e processo fundamental com regime-switching.

## Build e Testes

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sim_headless analysis_stylized_facts
ctest --test-dir build          # 21/21 testes
```

## CLI Headless

```bash
./build/sim_headless --seed 42 --duration 50000 --output logs/run.bin
./build/analysis_csv_export logs/run.bin out/
```

## Branch Atual

`refactor/microstructure` — em fase 9 completa (8/8 fatos estilizados), próxima é merge em `main`.

## Documentação Detalhada

- [docs/ARCHITECTURE_OVERVIEW.md](./docs/ARCHITECTURE_OVERVIEW.md) — Visão completa: camadas, tick order, agentes, config
- [docs/STYLIZED_FACTS_VALIDATION.md](./docs/STYLIZED_FACTS_VALIDATION.md) — Os 8 fatos, diagnóstico de ACF, drift compensatório

## Convenções

- Todos os agentes herdam `AgentBase`. Nova estratégia: criar `src/agents/MeuAgente.{h,cpp}`, adicionar em `Config.h` e registrar em `AgentFactory.cpp`.
- **Determinismo**: nenhum `std::random_device`, toda aleatoriedade via `RngService::for_consumer(id)`.
- **Tick order** em `SimulationLoop::tick_once` é fixo e documentado no código — não alterar a sequência.
- Preços são `int64_t` em tick-units (ex: 10000 = $100.00 com tick_size=0.01).
- Log binário via `BinaryLogWriter/Reader` com schema versionado (`EventSchema.h`).

## Parâmetros Críticos (não alterar sem validar 8/8 fatos)

- `RegimeSwitchingProcess.h`: `mu_low = mu_high = +7.3e-5` (drift compensatório — sem isso o fundamental colapsa)
- `MarketMakerAS.cpp`: TTL = `snap.base.tick + 2` (off-by-one fix)
- `NewsReactorParams::lag_range = {0, 1}` (lags maiores criam ACF positivo)
- `MomentumParams::count = 0` (momentum traders violam Fato 2 por design)
