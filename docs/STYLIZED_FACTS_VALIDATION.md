# Validação: Price Discovery e Fatos Estilizados

## Ordem das perguntas

Antes de perguntar se os retornos têm caudas gordas, é preciso perguntar se o preço negociado tem
alguma relação com o valor fundamental. Um mercado onde o mid não segue o fundamental pode passar
em fatos estilizados por acidente — ruído tem caudas também. Por isso a validação é feita em duas
camadas:

1. **Price discovery** — o mid rastreia o valor latente `V`? O book fica de dois lados? Algum tipo
   de agente está parado no limite de posição, virando uma parede em vez de negociar?
2. **Fatos estilizados** — dado que o preço é formado, ele tem as propriedades estatísticas de um
   mercado real?

Nenhuma das duas camadas é decidida em uma seed. `sim_calibrate` roda um ensemble e reporta
média ± desvio e a fração de seeds que passa cada critério.

```text
logs/run.bin  (schema v3: o snapshot carrega fundamental_value)
    │
    ▼
BinaryLogReader  →  Report::run(path, mm_ids)
    ├── log_returns(mid)              → curtose, skew, ACF, Hill
    ├── log(mid_t / V_t)              → gap, meia-vida, corr por horizonte
    ├── spread > 0                    → book de dois lados, CoV do spread
    └── maker/taker ∈ mm_ids          → volume MM-vs-MM
```

O `mid` nunca é ancorado no fundamental. Quando um lado do book esvazia, o publisher carrega o
último mid **formado pelo mercado** (`prev_valid_mid_`); escrever `V` ali criaria price discovery
por construção e a métrica mediria a si mesma.

---

## Camada 1 — Price discovery (10 seeds × 50k ticks)

| Métrica | Valor | Critério | Seeds |
|---|---|---|---|
| corr(rV, r_mid), h=1 | 0,088 ± 0,049 | — | — |
| corr(rV, r_mid), h=5 | 0,303 ± 0,041 | — | — |
| corr(rV, r_mid), h=20 | 0,641 ± 0,026 | > 0,30 | 10/10 |
| corr(rV, r_mid), h=100 | 0,859 ± 0,021 | > 0,50 | 10/10 |
| std(log(mid/V)) | 0,0018 ± 0,0004 | < 0,01 | 10/10 |
| meia-vida do gap | 27,2 ± 13,6 ticks | < 50 | 9/10 |
| book de dois lados | 0,972 ± 0,009 | alto | — |
| retornos exatamente zero | 0,207 ± 0,009 | — | — |
| volume MM-vs-MM | 0,000 | < 0,05 | 10/10 |
| pior tipo no limite de posição | 0,000 | < 0,05 | 10/10 |

A correlação crescer com o horizonte é o resultado esperado, não um defeito: em 1 tick o retorno do
mid é dominado por microestrutura (bounce, granularidade de 1 tick de preço, chegada discreta de
ordens) e o sinal fundamental só emerge quando acumulado. O erro de precificação é estacionário e
volta a zero em ~27 ticks, o que é o que "descoberta de preço" significa operacionalmente.

**Pior tipo no limite** é a métrica de ecologia viva. Um tipo de agente que passa a simulação
encostado no seu limite de posição não está escolhendo nada — o RiskGate rejeita um lado e o resto
do fluxo dele vira empurrão direcional espúrio. Na primeira rodada de ensemble apenas 20% das seeds
mantinham todos os tipos abaixo de 5%; hoje são 10/10 em 0%.

---

## Camada 2 — Os 8 fatos estilizados

Ensemble de 10 seeds × 50k ticks (`ensemble.txt` na pasta da spec):

| # | Fato | Média | Threshold | Seeds |
|---|---|---|---|---|
| 1 | Caudas gordas (curtose em excesso) | 81,12 ± 87,71 | > 1,0 | 10/10 |
| 2 | ACF de retorno ≈ 0 (lags 1 e 2) | 0,149 ± 0,006 | < 0,05 | **0/10** |
| 3 | Clustering de volatilidade, ACF(\|r\|, 1) | 0,084 ± 0,010 | > 0,05 | 10/10 |
| 4 | Memória longa de vol, média ACF(\|r\|, 1..10) | 0,055 ± 0,008 | > 0,02 | 10/10 |
| 5 | Assimetria ganho-perda, skew < 0 | −0,348 ± 1,493 | < 0 | **6/10** |
| 6 | Índice de cauda de Hill α | 4,59 ± 0,47 | ∈ [1,8; 6,0] | 10/10 |
| 7 | Variação do spread, CoV | 0,228 ± 0,029 | > 0,08 | 10/10 |
| 8 | Clustering de OFI, ACF(\|OFI\|, 1) | 0,165 ± 0,008 | > 0,03 | 10/10 |

**Total: 6,6/8 em média, 82,5% dos pares (fato, seed).**

Diagnósticos que acompanham: Ljung-Box Q(10) = 2820 ± 318, ACF do sinal de trade em lag 1 =
0,311 ± 0,010 (faixa alvo 0,10–0,40), correlação vol-volume = 0,099 ± 0,009, ~239 mil trades por
rodada.

### De onde vem cada fato

- **1, 6 — caudas.** Não vêm das innovations Student-t do fundamental. Vêm do fatiamento de ordens
  (`InstitutionalExecutor`, parents Pareto α=1,5 fatiados em filhos Pareto) e das cascatas de
  `StopLossCluster`. O teste de emergência abaixo é a evidência.
- **3, 4 — clustering e memória.** Regime-switching persistente (duração esperada 100 / 33 / 6,7
  ticks) mais o feedback do inventário do market maker: uma sequência de execuções de um lado
  alarga o spread cotado, o que amplifica o próximo retorno.
- **7 — spread.** O `MarketMakerAS` cota `half = half_min + vol_mult·σ_ticks + λ_adv·|OFI|`, então o
  spread responde a volatilidade e a toxicidade de fluxo. CoV medido só em ticks com book de dois lados.
- **8 — OFI.** Oito `NewsReactor` reagindo dentro da janela do evento, mais o informado de Kyle
  negociando o gap de precificação de forma persistente até fechá-lo.

---

## Falhas medidas (não corrigidas por threshold)

### Fato 2 — ACF de retorno ≈ 0 falha em 0/10 seeds, a 0,149

A autocorrelação não é um bug: é o preço de ter fatiamento de ordens. `ACF(sinal de trade, 1) =
0,31` é ele mesmo um fato estilizado (Lillo-Mike-Farmer) e é o que produz as caudas gordas. Como o
market maker move a crença com o fluxo (`belief_ += λ_kyle · OFI`), fluxo autocorrelacionado vira
retorno autocorrelacionado.

Varredura de `lambda_kyle` (o único parâmetro que controla diretamente esse repasse):

| `lambda_kyle` | ACF de retorno | Clustering (Fato 3) | corr(h=100) | Meia-vida do gap |
|---|---|---|---|---|
| 0,00 | passa em 75% das seeds | 0,011 | 0,41 | 377 ticks |
| 0,01 | passa em 75% das seeds | 0,023 | 0,62 | 253 ticks |
| **0,06** | **0,149** | **0,084** | **0,86** | **27 ticks** |
| 0,12 | 0,21 | — | — | — |
| 0,20 | 0,29 | — | — | — |
| 0,35 | 0,35 | — | — | — |

Zerar o repasse compra o Fato 2 e paga com o Fato 3, o Fato 4 e o price discovery — o mid deixa de
seguir o fundamental porque o maker deixa de aprender com o fluxo. `0,06` é o melhor compromisso
conjunto encontrado dentro das faixas economicamente justificáveis. O fato fica registrado como
falha; mexer no threshold seria redefinir o fato para que ele passe.

### Fato 5 — assimetria ganho-perda passa em 6/10 seeds

Média −0,348 com desvio entre seeds de 1,493: o desvio é 4× a média. Com 50k ticks a skewness é
determinada por um punhado de eventos de cauda, então o estimador não tem resolução nessa amostra.
O sinal está na direção certa (negativo) mas não é distinguível de zero. Isso é limite de amostra,
não defeito da ecologia; rodadas mais longas são o caminho, não recalibrar agentes.

---

## Teste de emergência: innovations gaussianas

A pergunta é se os fatos são produzidos pelo mercado ou herdados da distribuição de entrada.
`--gaussian` troca as innovations Student-t(ν=3,5) do fundamental por normais e mantém tudo o mais
igual, inclusive as seeds.

| # | Fato | Student-t | Gaussiano |
|---|---|---|---|
| 1 | Caudas gordas (curtose) | 81,12 (10/10) | 85,22 (10/10) |
| 3 | Clustering de vol | 0,084 (10/10) | 0,083 (10/10) |
| 4 | Memória longa | 0,055 (10/10) | 0,057 (10/10) |
| 6 | Hill α | 4,59 (10/10) | 4,57 (10/10) |
| — | corr(rV, r_mid) h=100 | 0,859 | 0,862 |
| — | Total | 6,6/8 (82,5%) | 6,6/8 (82,5%) |

As caudas sobrevivem intactas. Isso é a evidência de que a curtose de 81 é gerada pela
microestrutura — fatiamento de ordens, cascatas de stop e retirada de liquidez do maker — e não
copiada da cauda do input.

---

## Como investigar uma regressão

```bash
# 1. O ensemble é a unidade de medida, nunca uma seed
./build/sim_calibrate --seeds 10 --duration 50000 --outdir /tmp/cal/

# 2. Ecologia primeiro: algum tipo parado no limite?  A tabela por tipo do
#    sim_headless mostra Σ|posição| e % de ticks no limite.
./build/sim_headless --seed 42 --duration 50000 --output /tmp/A.bin

# 3. Price discovery antes de fatos: se corr(h=100) caiu, nenhum número da
#    tabela de fatos significa coisa alguma.

# 4. Determinismo (dois hashes iguais):
./build/sim_headless --seed 42 --duration 50000 --output /tmp/B.bin
shasum -a 256 /tmp/A.bin /tmp/B.bin
```

Se um fato mudou depois de uma alteração em agente, o suspeito é o **stream de RNG**, não a
economia: um sorteio feito sob condição de estado (posição, inventário) faz o stream depender do
estado e desloca toda a trajetória. Sortear sempre, decidir depois.

---

## Referências de código

| Arquivo | Responsabilidade |
| --- | --- |
| `src/analysis/Report.{h,cpp}` | Os 8 fatos, price discovery, leitura do log v3 |
| `src/analysis/AcfComputer.{h,cpp}` | ACF (estimador biased) e ACF de valores absolutos |
| `src/analysis/HillEstimator.{h,cpp}` | Índice de cauda multi-k |
| `src/tools/sim_calibrate.cpp` | Harness de ensemble, `--gaussian`, `--save` |
| `src/tools/sim_headless.cpp` | Uma rodada, tabela por tipo de agente, relatório inline |
| `src/marketdata/MarketDataPublisher.cpp` | Mid carry-forward sem âncora no fundamental |
