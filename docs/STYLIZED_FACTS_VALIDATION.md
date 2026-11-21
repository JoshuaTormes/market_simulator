# Validação: Price Discovery e Fatos Estilizados

## Ordem das perguntas

Antes de perguntar se os retornos têm caudas gordas, é preciso perguntar se o preço negociado tem
alguma relação com o valor fundamental. Um mercado onde o mid não segue o fundamental pode passar
em fatos estilizados por acidente — ruído tem caudas também. Por isso a validação é feita em duas
camadas:

1. **Price discovery** — o mid rastreia o valor latente `V`, e qual parte de `V`? O book fica de dois
   lados? Algum tipo de agente está parado no limite de posição, virando uma parede em vez de negociar?
2. **Fatos estilizados** — dado que o preço é formado, ele tem as propriedades estatísticas de um
   mercado real, na escala de tempo em que a literatura as mede?

Nenhuma das duas camadas é decidida em uma seed. `sim_calibrate` roda um ensemble e reporta
média ± erro padrão e a fração de seeds que passa cada critério, separando o que pode orientar
ajuste do que só julga (ver o protocolo abaixo).

```text
sim_calibrate: SeriesCollector (em memória)      sim_headless: logs/run.bin (schema v4)
    │                                                 │  BinaryLogReader (lê v2, v3, v4)
    └──────────────────────┬──────────────────────────┘
                           ▼
            Report: extract (séries) → compute (métricas)
    ├── mid, V, news_cum_log por tick     → gap, meia-vida, corr com V, com a difusão, com as notícias
    ├── retornos de calendário 1/5/30 min → bateria v2 (StylizedFactsV2), IC por bootstrap
    ├── retornos de 1 s                   → bateria legada (diagnóstico)
    ├── registros Metaorder               → δ de metaordem, prefator Y, permanente/temporário
    ├── trades com maker/taker            → volume maker contra maker, ACF do sinal, quem paga quem
    └── ledger no último tick             → soma zero, P&L por classe, taxas
```

O `mid` nunca é ancorado no fundamental. Quando um lado do book esvazia, o publisher carrega o
último mid **formado pelo mercado** (`prev_valid_mid_`); escrever `V` ali criaria price discovery
por construção e a métrica mediria a si mesma.

---

## Protocolo de calibração e validação

Um modelo ajustado olhando as mesmas métricas que depois o validam vai passar nelas por construção.
Por isso as métricas estão divididas em conjuntos, congelados em `src/analysis/Protocol.h`
**antes** de qualquer mudança de mecanismo.

| Conjunto | Para que serve | Métricas | Seeds |
|---|---|---|---|
| **CAL** — calibração | pode orientar ajuste de parâmetro | vol anual do mid (1 min, alvo ≈ 0,30), spread médio em bp, razão U de volume (> 1,30), share do maior maker (< 0,50), meia-vida do gap mid/V (< 50 ticks) | 1–10 |
| **VAL** — validação (hold-out) | julga uma mudança; **nunca** orienta ajuste | bateria v2 (v2.1–v2.10 e o número de PASSA), δ de metaordem (faixa empírica [0,4; 0,7]), corr(r_mid, r_difusão) em h = 20 (> 0,30) e h = 100 (> 0,50) | 101–120 |
| diag | reportado e gravado, não orienta nem julga | bateria legada, price discovery total, P&L por classe, contadores de ecologia, expoente por tick (eco da regra do maker) | — |

Iteração em 60 pregões; a rodada final é VAL em 250 pregões.

**Regras**

1. **A VAL não orienta ajuste.** Cada escolha de parâmetro cita a métrica CAL que a motivou, e a VAL
   roda uma vez por decisão, depois de a escolha estar feita. Se um parâmetro for escolhido olhando
   um número VAL, isso é registrado no plano e as seeds VAL são trocadas por uma faixa nova.
2. **Comparação é sempre pareada.** Duas configurações rodam nas mesmas seeds, e o que se compara é
   a diferença seed a seed, Δᵢ = atualᵢ − baseᵢ. O erro padrão é SE(Δ) = dp(Δ)/√n.
3. **Uma mudança só é melhora ou piora se |Δ| > 2·SE(Δ).** Quando a métrica tem alvo, o teste é
   feito sobre a distância ao alvo, não sobre o valor. Abaixo disso, o veredito é "dentro do ruído".
4. **P&L tem só teste de sinal.** Mais lucro do maker não é "melhor": o excedente sai de alguém.
5. **Thresholds são a definição do fato.** Não se mexe neles para passar, nem na bateria legada.
6. **Rodadas fora das faixas de seed** (`--seeds N` sem `--seed-set`) aparecem como "livre, fora do
   protocolo". Servem para explorar, não para decidir.

**Uso**

```bash
# Rodada de calibração; grava per_seed.csv (uma linha por seed × métrica)
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir logs/cal/

# Mesma faixa com uma mudança, pareada contra a rodada anterior
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir logs/cal_mm1/ \
    --set mm.count=1 --baseline logs/cal/per_seed.csv

# Julgamento de uma mudança já escolhida
./build/sim_calibrate --seed-set val --days 60 --jobs 8 --outdir logs/val/ \
    --baseline logs/val_antes/per_seed.csv
```

O CSV tem uma linha de cabeçalho `#` com as seeds, a duração, as innovations e os overrides. Quando
o CSV é usado como `--baseline`, essa linha é repetida ao lado da linha da rodada atual, para que
uma comparação entre mundos diferentes não passe sem ser vista. A ferramenta confere na partida que
toda chave do `Protocol.h` corresponde a uma métrica calculada, e sai com erro se não corresponder.
Argumento desconhecido ou flag sem valor também saem com erro (código 2); no zsh, confira a linha
`[INFO] seeds=… ticks=…` antes de ler um ensemble.

**Ablações (`--set`).** `mom.qty=0` e `mr.qty=0` zeram a ordem depois de todos os sorteios, então
calam a coorte sem mexer no stream de nenhum outro agente. As chaves `*.count` mudam quantos agentes
são sorteados e, com isso, todo sorteio que vem depois: a comparação pareada ainda vale, mas mede
população e sorteios juntos. Exceção: `lot.count=0` reproduz a referência sem LOTs bit a bit, porque
os LOTs são construídos por último. `mm.impact_q_ref=0` volta à regra linear de Kyle.

**Contabilidade.** Toda posição inicial (a coorte de stop-loss nasce comprada ou vendida) tem
contraparte numa conta reservada, `kAllocationAccount`. Com isso Σ posições = 0 e Σ P&L bruto = 0
valem exatamente, em inteiros, e o relatório confere as duas por seed. Taxas são contadas em
1/1000 de tick (`kFeeUnitsPerTick`), e o relatório confere Σ P&L líquido = −Σ taxas. A tabela de
P&L por classe diz quem paga quem.

**Horizonte longo.** O relatório traz, por seed, log(mid/V) no fechamento de cada pregão e a fração
do pregão com book de dois lados. Uma seed com algum pregão abaixo de 50% de book de dois lados é
tratada como colapsada: os fatos dela saem à parte e não entram nas médias.

---

## Referência atual (`r11`, 60 pregões)

Arquivos em `specs/plans/2026-11-18_correcao-circularidade-validacao/resultados/r11_cal.*` (10 seeds)
e `r11_val.*` (20 seeds). Média ± SE entre seeds; "seeds" é a fração que passa o critério.

### Camada 1 — Price discovery

`V` tem duas partes: a difusão (que só informados e LOTs veem, com erro) e as notícias (que o
maker lê pelo EventBus no tick do anúncio). O schema v4 grava `news_cum_log` e permite separar as duas.

| Correlação com r_mid | conj | CAL | VAL | critério |
|---|---|---|---|---|
| r_difusão, h=20 | VAL | 0,267 ± 0,009 (2/10) | 0,263 | > 0,30 |
| r_difusão, h=100 | VAL | **0,488 ± 0,009** (2/10) | **0,469 ± 0,009** | > 0,50 |
| r_V total, h=100 | diag | 0,801 ± 0,010 | 0,820 | — |
| r_notícias, h=100 | diag | 0,652 | 0,686 | — |
| r_V longe das notícias, h=100 | diag | 0,650 | 0,655 | — |

| Métrica | conj | CAL | VAL | critério |
|---|---|---|---|---|
| meia-vida do gap mid/V | CAL | 281 ± 13 ticks (0/10) | 271 ± 12 | < 50 |
| dp(log(mid/V)) | diag | 0,29% | 0,29% | < 1% |
| book de dois lados (pior pregão) | diag | 0,9885 (0,979) | 0,988 | > 0,99 |
| volume maker contra maker | diag | 0,138 (0/10) | 0,131 | < 0,05 |
| topo do book fora dos makers | diag | 0,0167 | 0,0171 | — |
| pior tipo no limite de posição | diag | 0,0001 | — | < 0,05 |

**A parte difusiva de V é incorporada pela metade em 100 s.** O "corr 0,77" das versões anteriores
é majoritariamente notícia, que o maker lê de graça. A Etapa 10 mostrou que isso não é circular pelo
canal da leitura: com erro de leitura até s = 1 a corr com as notícias não cai (0,655 → 0,647), porque
os outros corrigem em poucos ticks. Só o maker cego derruba essa corr (0,36), e a bateria v2 cai para 3,4.

### Camada 2 — Bateria v2 (retornos de calendário)

Cada fato é medido na escala em que a literatura o mede (Cont 2001), com IC por bootstrap em blocos.
O estado é PASSA, FALHA ou INCONCLUSIVO (IC largo demais para decidir).

| # | Fato | Escala | Critério | Valor CAL | CAL P/F/I | VAL P/F/I |
|---|---|---|---|---|---|---|
| v2.1 | Caudas gordas | 5 min | curtose > 1, IC exclui 0 | 26,6 | 10/0/0 | 20/0/0 |
| v2.2 | Gaussianidade agregacional | 1 → 30 min | curtose decresce | — | 10/0/0 | 20/0/0 |
| v2.3 | Ausência de autocorrelação | 1 e 5 min | \|ACF(1..5)\| < 2/√N | ρ = −0,148 | **0/10/0** | **0/20/0** |
| v2.4 | Clustering de vol | 5 min | ACF(\|r\|, 1) > 2/√N | 0,058 | 10/0/0 | 18/2/0 |
| v2.5 | Memória longa de vol | 1 min, lags 1–390 | lei de potência, β ∈ (0,05; 0,6), melhor que exponencial | β = 0,75 | **0/10/0** | **2/18/0** |
| v2.6 | Assimetria ganho-perda | 30 min | skew < 0, IC exclui 0 | −0,04 | 1/0/9 | 1/0/19 |
| v2.7 | Índice de cauda | 5 min | Hill α ∈ [2; 5] | 2,47 | 10/0/0 | 20/0/0 |
| v2.8 | Efeito alavancagem | 5 min | corr(r_t, \|r_{t+k}\|) < 0 e significativa | 0,003 | **0/10/0** | **0/20/0** |
| v2.9 | Correlação vol-volume | 5 min | > 2/√N | 0,072 | 10/0/0 | 18/2/0 |
| v2.10 | Memória do sinal de trade | por trade | lei de potência, γ ∈ (0,2; 0,9) | γ = 0,90 | **3/7/0** | **3/17/0** |

**PASSA: 5,4 ± 0,16 de 10 (CAL), 5,1 ± 0,12 (VAL).**

### Bateria legada (1 s, diagnóstico)

Os 8 fatos originais, medidos em retornos de 1 s. Ficam como diagnóstico porque a escala de 1 s está
dominada por microestrutura (bounce, granularidade de tick, chegada discreta) e não é a escala em que
os fatos foram documentados.

| # | Fato | Threshold | CAL | seeds | VAL seeds |
|---|---|---|---|---|---|
| 1 | Caudas gordas (curtose) | > 1,0 | 1195 | 10/10 | 20/20 |
| 2 | ACF de retorno ≈ 0 (lags 1 e 2) | < 0,05 | 0,058 ± 0,003 | **3/10** | 9/20 (0,051) |
| 3 | Clustering de vol, ACF(\|r\|, 1) | > 0,05 | 0,038 ± 0,004 | **2/10** | 1/20 |
| 4 | Memória longa, média ACF(\|r\|, 1..10) | > 0,02 | 0,028 | 10/10 | — |
| 5 | Assimetria ganho-perda, skew < 0 | < 0 | −0,45 ± 1,36 | 5/10 | — |
| 6 | Hill α | ∈ [1,8; 6,0] | 2,52 | 10/10 | — |
| 7 | CoV do spread | > 0,08 | 0,516 | 10/10 | — |
| 8 | Clustering de OFI, ACF(\|OFI\|, 1) | > 0,03 | 0,190 | 10/10 | — |

**6,0/8 (CAL), 6,05 ± 0,20 (VAL).**

### Camada 3 — Microestrutura e economia

| Métrica | conj | CAL | VAL | critério |
|---|---|---|---|---|
| δ de metaordem (I ∝ Q^δ) | VAL | 0,72 ± 0,05 (3/10) | 0,73 ± 0,04 (9/20) | [0,4; 0,7] |
| prefator Y | diag | 1,35 | 1,27 | ~1 |
| impacto permanente/temporário | diag | 0,70 | 0,72 | — |
| expoente por tick (eco da regra) | diag | 0,575 | 0,588 | — |
| vol anual do mid / de V (1 min) | CAL | 0,334 / 0,287 | 0,347 / 0,306 | ≈ V |
| spread médio | CAL | 2,68 bp | 2,71 bp | ~3 bp |
| razão U de volume | CAL | 1,58 (10/10) | 1,57 | > 1,30 |
| share do maior maker | CAL | 0,40 (10/10) | 0,385 | < 0,50 |
| ACF do sinal de trade, lag 1 | diag | 0,142 (10/10) | 0,145 | [0,10; 0,40] |
| flash crashes por rodada | diag | 18,5 | 18,7 | < 2 |

**Quem paga quem (US$ mil por 60 pregões, média entre seeds; seeds com P&L > 0):**

| Classe | CAL | VAL | Classe | CAL | VAL |
|---|---|---|---|---|---|
| informados (2) | +347 (10/10) | +324 (20/20) | noise (25) | −368 | −361 |
| makers (3) | +122 (10/10) | +126 (19/20) | momentum (3) | −97 | −91 |
| LOT (20) | +61 (10/10) | +70 (20/20) | institucional (2) | −58 | −58 |
| mean-reverter (2) | +5,9 | +4,4 | reatores (8) | −12 | −14 |
| stop-loss (6) | +0,8 | — | Σ | 0 | soma zero exata 10/10 e 20/20 |

Que **informados e makers** fiquem positivos ao mesmo tempo é o teste econômico mais duro da mesa: o
informado ganha do fluxo desinformado, o maker ganha do spread, e nenhum dos dois vive de subsidiar
o outro. O parâmetro que decide isso é o spread: a 11–12 bp o excedente inteiro ia para os makers e
o informado era o maior perdedor da mesa. Com taxa de taker de 0,1 bp os dois seguem positivos em
10/10 seeds; os informados só zerariam com ~2,1 bp (Etapa 12; nenhum agente vê a taxa, então ela só
redistribui para a bolsa).

### De onde vem cada fato

- **v2.1, v2.7 — caudas a 5 min.** Sobrevivem com innovations gaussianas (ver o teste de emergência),
  então não são copiadas do input. Vêm das notícias (saltos t(3) em `log V`), do fatiamento de ordens
  (`InstitutionalExecutor`, parents Pareto α=1,5) e das cascatas de `StopLossCluster`.
- **Caudas a 1 s (Fato legado 1, 6).** São as notícias: com o maker cego a curtose a 1 s cai de 1213
  para 3,1 e o Hill vai a 8,2 (Etapa 10).
- **v2.4 — clustering a 5 min.** Regime-switching persistente (2 h / 30 min / 3 min) mais a interação
  entre makers de latência diferente: com 1 maker o clustering e o v2.9 somem (Etapa 8).
- **v2.9 — vol-volume.** Depende da leitura da notícia pelo maker: o maker cego leva o v2.9 de 10/10 a 0/10.
- **Fato legado 7 — spread.** `half = half_min + vol_mult·σ_ticks + λ_adv·|OFI|`, então o spread
  responde a volatilidade e a toxicidade de fluxo.
- **Fato legado 8 — OFI.** Reatores de notícia dentro da janela do evento, o informado fechando o gap
  de forma persistente, e a assincronia de recotação, que espalha a resposta do book por vários ticks.
- **ACF do sinal de trade.** Fluxo agressivo que cruza contra ordens paradas de um mesmo LOT vira
  sequência de fills do mesmo lado: os LOTs levaram a ACF de 0,101 para 0,142.

### Horizonte longo: VAL × 250 pregões

`resultados/final_val250` vem da rodada final, feita uma vez só. Os primeiros 60 pregões repetem
`r11_val` bit a bit, então a comparação pareada isola o efeito do horizonte.

**O que não muda (estacionário):**
- nenhuma seed colapsa e a soma é zero;
- as taxas por pregão (P&L de cada classe, 0,31 flash crash por pregão) ficam constantes;
- spread (2,72 bp), razão U, maior maker (0,38), MM×MM (13%), price discovery (corr com a difusão
  0,469 a h=100) e a bateria v2 (5,1) ficam iguais.

**O que muda, e por quê:**
- **δ de metaordem:** cai de 0,73 para **0,50 ± 0,03** (16/20 seeds na faixa). A causa é a amostra,
  não a dinâmica: com 4× mais parents, o ajuste por faixas geométricas de Q/ADV ganha faixas nas
  pontas. **O δ só se compara no mesmo horizonte.**
- **Hill a 1 s:** 2,46 → 2,25.
- **v2.5:** β 0,74 → 0,86.
- **Maior |gap| de fechamento:** 0,70% → 1,06%; o pior caso é 2,0% e só 10/20 seeds ficam abaixo
  de 1%.

Os três últimos andam porque uma amostra maior traz mais eventos de cauda.

---

## Números antigos que não valem mais

A Etapa 8 do plano refez as medidas antigas com o protocolo. Três números que apareciam como
resultado eram o modelo lendo a si mesmo.

**"Expoente de impacto 0,51 ± 0,005".** Era o expoente por tick da regressão |Δmid| ~ |OFI|^β. O maker
move a crença por `λ·sign(OFI)·sqrt(|OFI|·q_ref)`, então β ≈ 0,5 é a regra côncava lida de volta: com
a regra linear (`mm.impact_q_ref=0`) o mesmo estimador dá 1,00. A medida que não é eco da regra é o δ
de metaordem, no registro `Metaorder` de cada parent institucional concluído. Ele responde à regra,
mas não a copia: 0,69 côncavo contra 0,89 linear na Etapa 8 (hoje 0,72 ± 0,05). O prefator Y ≈ 1,3 e a
razão permanente/temporário ≈ 0,70 não dependem da regra.

**"corr(rV, r_mid) h=100 = 0,77".** É majoritariamente notícia (ver a Camada 1). A métrica que julga
price discovery é a correlação com a difusão de V: 0,49.

**"7,0/8 fatos".** O Fato legado 3 que passava (0,107) era a grade da cadência fixa dos makers (próxima
seção). Sem ela o clustering a 1 s é ≈ 0,01 e o fato falha honestamente; a 5 min (v2.4) passa.

**Nota sobre momentum e mean-reverter.** A documentação antiga dizia que o par "segura a ACF de
retorno" (um terceiro mean reverter dava 0,062, dois momentum 0,056). Essas medidas mudavam `count` e
com isso o stream de todos os agentes. Com os agentes presentes e quantidade zero (sorteios intactos), o
Fato 2 legado melhora (0,061 → 0,049) e nenhuma métrica piora. Os dois ficam como fluxo pagador: o
momentum é o maior pagador do maker.

---

## Cadência de recotação: a grade de período 6

A revisão anterior achou que os makers recotando em lockstep gravavam o ciclo de recotação no mid, e
escalonou a cadência: maker `i` recotava a cada `1 + (i % 3)` ticks com fase `i`. Isso levou o Fato
legado 3 de 0/10 a 10/10 e o total legado a 7,0/8.

A Etapa 9 olhou a ACF inteira e o perfil por fase do tick. Os períodos 1, 2 e 3 escalonados formam uma
**grade de período 6**: ACF(|r|) de 0,13 / 0,23 / 0,14 nos lags 1–3 e 0,21 no lag 6, e P(mid anda) de
0,55 a 0,89 conforme `tick % 6`. O escalonamento não removeu o artefato, espalhou-o, e o Fato 3 que
passava era essa grade.

A correção é a cadência **sorteada**: o intervalo até a próxima recotação é uniforme em
{1, …, 2p − 1}, com média p, sorteado em toda chamada (o stream não depende do estado), e o TTL é
`tick + intervalo + 2`. P(r ≠ 0) fica igual em toda fase (0,73). Junto, cada maker passou a acumular o
OFI desde a última recotação (`accumulate_ofi`). Sem isso o maker de período 1 era o único a absorver
todo o fluxo, era selecionado contra e perdia ~US$ 300 mil por 60 pregões, até cruzar o piso de
drawdown (Etapa 8b). `lambda_kyle` foi recalibrado na CAL para 0,045 (com 0,06 a vol do mid ia a 0,375).

O canal que sobra é o de Budish, Cramton & Shim (2015): o maker lento carrega cotação defasada e é
cruzado pelo rápido. O volume maker contra maker caiu de 21% para 14%; ainda é alto. Um cancelamento
reativo dentro do tick mudaria a tick order e ficou fora de escopo.

---

## O que cada etapa mudou (CAL, 60 pregões, pareado)

| Etapa | Mudança | Efeitos significativos (\|Δ\| > 2·SE) |
|---|---|---|
| 8b | Limite de drawdown desligado (`max_drawdown_pct = 1,0`); ordem que reduz nunca é bloqueada | O piso 1e8·0,20 era uma barreira absorvente: o maker 0 saía no pregão ~40 e o mercado seguia com 2. Em 250 pregões: share do maior maker 0,52 → 0,40, book de dois lados 0,967 → 0,987. v2.5 e v2.10 passavam por artefato (quebra de nível gera memória longa espúria; Diebold & Inoue 2001) e passaram a falhar: v2 5,9 → 5,0 |
| 9 | Cadência sorteada + OFI acumulado, λ = 0,045 | Grade removida (Fato legado 3 0,107 → 0,014). Vol do mid 0,355 → 0,333, MM×MM 0,211 → 0,151, spread 3,38 → 2,77 bp, ACF de sinal entra no alvo; meia-vida do gap 166 → 269 (piora) |
| 10 | Erro na leitura da notícia pelo maker (`news_signal_noise`) | s até 1 não derruba a corr com notícias; s > 0 só cria clustering transiente depois de cada notícia. Padrão s = 0 (idêntico a r9) |
| 11 | 20 `LimitOrderTrader` (Chiarella-Iori) | topo fora dos makers 0 → 1,7%; spread 2,77 → 2,68 bp; ACF de sinal 0,101 → 0,142; v2 5,0 → 5,4; MM×MM 0,151 → 0,138; P&L dos makers 159 → 122 mil |
| 12 | Taxas em bp do nocional (teste) | 0,1 bp de taker: informados e makers positivos em 10/10, Σ líquido = −Σ taxas exato. Padrão 0 |

VAL de cada decisão (rodada uma vez): concorda em sinal com a CAL em todas as métricas de mecanismo.
Os detalhes, inclusive as variantes rejeitadas, estão no `plano.md` da revisão.

---

## Orçamento de variância do fundamental

`sigma_annual = 0,30` estava configurado e o fundamental realizava **41–42%** — medido em 200k e
500k ticks, então não era ruído amostral. Não era a normalização estacionária dos regimes nem a
padronização da Student-t (ambas verificadas corretas).

A causa: as notícias públicas entram no mesmo `log V` por `apply_shock()`, e a variância delas nunca
esteve no orçamento. Com 8 eventos por dia, a escala antiga de 0,4% e caudas t(3):

```
var_notícia/tick = λ · escala² · ν/(ν−2) = (8/23400) · 0,004² · 3 = 1,64e-8
var_total/tick   = (0,30/√(23400·252))²                          = 1,53e-8
```

O fluxo de notícias sozinho carregava **108%** do orçamento inteiro. Vol total prevista
`√(0,09 + 0,0968) = 43,2%`, contra 41–42% medidos.

A correção segue Merton (1976): a variância total do processo de valor é a parte difusiva mais a
parte de saltos, então `FundamentalConfig::news_var_per_tick` é subtraída e a difusão fica com o
resto. A repartição entre as duas metades é ela mesma um parâmetro econômico — a 25% para as
notícias o índice de Hill subia para 6,3 e o Fato 6 caía, porque a metade de saltos é o que fornece
a massa de cauda. A escala atual, `impact_scale = 0,00273`, põe as notícias em metade do orçamento:

```
var_notícia/tick = (8/23400) · 0,00273² · 3 = 7,6e-9   (50% de 1,53e-8)
```

Vol de V medida a 1 min: **0,287 (CAL), 0,306 (VAL)**. O déficit contra 0,30 é o viés de baixo do
estimador de variância numa amostra finita de uma t(3). `NewsReactorParams::impact_scale` tem de ser
igual a `NewsConfig::impact_scale`: é a referência contra a qual o tamanho da reação é cotado.

---

## Falhas medidas (não corrigidas por threshold)

- **v2.3 — o retorno a 1 min reverte** (ρ ≈ −0,15, 0/10 e 0/20). É a falha estrutural mais clara: o
  mid ultrapassa e volta. Candidatos: o passo de crença côncavo do maker, que reage mais a fluxo pequeno,
  e o impacto temporário dos filhos institucionais.
- **v2.5 — memória longa da vol ausente** (β ≈ 0,75, 0/10 e 2/20). A ACF de |r| decai rápido demais
  para uma lei de potência. A memória que aparecia antes era a quebra de nível da saída do maker 0.
- **v2.8 — efeito alavancagem ausente** (corr ≈ 0,003). `StopLossParams::long_bias` cria uma
  assimetria de stops, não o efeito de Black (1976), que vem do balanço da empresa; nada no modelo liga
  retorno negativo a vol futura.
- **v2.10 — memória do sinal de trade curta** (γ ≈ 0,9, 3/10 e 3/20). A ACF de lag 1 está no alvo, mas
  decai rápido demais: o fatiamento institucional é curto e os parents são poucos.
- **Fatos legados 2 e 3 a 1 s.** |ACF(r)| = 0,058 (negociação não-sincronizada entre makers de cadência
  diferente: ajuste parcial é autocorrelação de curto prazo, Lo & MacKinlay 1988) e ACF(|r|) = 0,038
  (sem a grade, não há clustering a 1 s).
- **Fato legado 5 / v2.6 — skew.** Inconclusivo em 9/10 e 19/20: a 30 min, 60 pregões dão ~780
  retornos, e a skewness é decidida por um punhado de eventos de cauda.
- **O gap fecha devagar**: meia-vida de 281 ticks contra o alvo de 50. A difusão de V chega ao preço
  só pelos 2 informados e pelos LOTs, que têm orçamento de posição limitado.
- **Volume maker contra maker 14%** e **~18 flash crashes por rodada** (0,3 por pregão; alvo < 2).

---

## Teste de emergência: innovations gaussianas

`--gaussian` troca as innovations Student-t(ν=3,5) do fundamental por normais e mantém tudo o mais
igual, inclusive as seeds. Se as caudas fossem copiadas do input, elas sumiriam.

Rodada `resultados/e13_gauss` (CAL, 60 pregões), pareada contra `r11_cal`:

| Fato | Student-t | Gaussiano | Δ pareado |
|---|---|---|---|
| v2.1 caudas gordas (curtose a 5 min) | 26,6 (10/10) | 27,8 (10/10) | ruído |
| v2.7 Hill α a 5 min | 2,47 (10/10) | 2,50 (10/10) | ruído |
| Fato legado 1, curtose a 1 s | 1195 (10/10) | 1192 (10/10) | ruído |
| Fato legado 6, Hill α a 1 s | 2,52 (10/10) | 2,53 (10/10) | ruído |
| v2.4 clustering a 5 min | 0,058 (10/10) | 0,059 (10/10) | ruído |
| v2.9 vol-volume | 0,072 (10/10) | 0,051 (9/10) | desce |
| bateria v2, PASSA | 5,4 | 5,1 | ruído |
| bateria legada | 6,0/8 | 6,2/8 | ruído |
| corr(r_mid, r_difusão) h=100 | 0,488 | 0,497 | ruído |
| vol anual de V (1 min) | 0,287 | 0,288 | ruído |

Nenhuma melhora nem piora significativa em CAL+VAL. As caudas sobrevivem intactas nas duas escalas, o
que é a evidência de que elas são geradas dentro do modelo e não copiadas da cauda das innovations
da difusão. O teste não isola a microestrutura: as notícias continuam entrando em `log V` com saltos
t(3), e a Etapa 10 mostrou que, a 1 s, as caudas são as notícias (com o maker cego a curtose cai para 3,1).

---

## Trabalho futuro (Bloco F da spec, adiado)

Dos itens que alteram mecanismos, só a ablação de momentum/mean-reverter (25) e os rótulos (28)
entraram nesta revisão. Ficam para depois:

- **Institucional (21).** Pareto na **duração** do parent, com o tamanho proporcional a duração ×
  participação; o resíduo deixa de ser descartado (o cronograma estende até concluir ou a execução
  parcial é registrada no `MetaorderRecord`). É o candidato natural para o v2.10.
- **Informado (22).** Erro de sinal persistente AR(1), com ρ derivado de uma meia-vida via `TimeScale`.
- **Reator de notícia (23).** Já desmonta a posição depois de um holding sorteado (`hold_range`);
  falta revisar o horizonte contra a meia-vida do gap.
- **Stop-loss (24).** Gatilho em múltiplos da vol diária, em vez de % fixo.
- **Processos alternativos (26).** `kappa` do OU a partir de uma meia-vida em dias; variância dos saltos
  do JumpDiffusion dentro do orçamento de `sigma_annual`.
- **Drift de Jensen (27).** Truncar a magnitude da notícia e compensar `−log E[exp(·)]` para que `E[V]`
  seja martingale.
- **Efeito alavancagem real.** Feedback retorno → vol (margem, EGARCH). O v2.8 mede; o mecanismo é
  decisão posterior.
- **Cancelamento reativo do maker dentro do tick**, para o volume maker contra maker. Muda a tick order.
- **Taxa na decisão dos agentes.** Hoje a taxa só redistribui P&L.

---

## Como investigar uma regressão

```bash
# 1. O ensemble é a unidade de medida, nunca uma seed; compare pareado
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir /tmp/cal/ \
    --baseline /tmp/cal_antes/per_seed.csv

# 2. Ecologia primeiro: algum tipo parado no limite?  A tabela por tipo do
#    sim_headless mostra Σ|posição| e % de ticks no limite.
./build/sim_headless --seed 42 --days 5 --output /tmp/A.bin

# 3. Price discovery antes de fatos: se a corr com a difusão caiu, nenhum
#    número da tabela de fatos significa coisa alguma.

# 4. Determinismo (dois hashes iguais):
./build/sim_headless --seed 42 --days 5 --output /tmp/B.bin
shasum -a 256 /tmp/A.bin /tmp/B.bin
```

Se um fato mudou depois de uma alteração em agente, o suspeito é o **stream de RNG**, não a
economia: um sorteio feito sob condição de estado (posição, inventário) faz o stream depender do
estado e desloca toda a trajetória. Sortear sempre, decidir depois. Antes de ler um fato a 1 s, olhe
a ACF inteira e o perfil por fase do tick: uma grade de cadência aparece como pico em lags múltiplos.

---

## Referências de código

| Arquivo | Responsabilidade |
| --- | --- |
| `src/analysis/Report.{h,cpp}` | extract/compute: price discovery, bateria legada, leitura do log v2/v3/v4 |
| `src/analysis/StylizedFactsV2.{h,cpp}` | Bateria v2 em retornos de calendário, IC por bootstrap em blocos |
| `src/analysis/Protocol.h` | Conjuntos CAL/VAL/diag, faixas de seeds, regra de 2·SE |
| `src/analysis/AcfComputer.{h,cpp}` | ACF (estimador biased) e ACF de valores absolutos |
| `src/analysis/HillEstimator.{h,cpp}` | Índice de cauda multi-k |
| `src/tools/sim_calibrate.cpp` | Ensemble paralelo em memória (`SeriesCollector`), média ± SE, CSV por seed, `--baseline` pareado, `--seed-set`, `--set`, `--gaussian`, taxas, P&L por classe |
| `src/tools/sim_headless.cpp` | Uma rodada, tabela por tipo de agente, relatório inline |
| `src/marketdata/MarketDataPublisher.cpp` | Mid carry-forward sem âncora no fundamental; fração do topo fora dos makers |
| `src/persistence/EventSchema.h` | Schema v4: `news_cum_log` no snapshot, registro `Metaorder` |
