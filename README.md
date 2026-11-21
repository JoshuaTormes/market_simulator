# Simulador de Microestrutura de Mercado

Este é um simulador de mercado de ações escrito em C++17. Ele reproduz um pregão segundo a segundo,
com um livro de ofertas real e 71 agentes que seguem estratégias diferentes.

O ponto central: **ninguém escreve o preço.** A empresa tem um valor "verdadeiro" que muda com o
tempo e com as notícias, mas esse valor fica escondido. O preço negociado nasce só do encontro
entre ordens de compra e de venda, e o que se mede é se o mercado consegue descobrir o valor
verdadeiro sozinho.

## Como funciona

Cada **tick** é 1 segundo de negociação. Um pregão tem 23.400 ticks (6,5 h), e um ano tem 252
pregões. A cada tick, na mesma ordem fixa:

1. **O valor fundamental anda.** O valor verdadeiro da empresa (`V`) muda um pouco. Ele alterna
   entre fases calmas (que duram horas) e fases de volatilidade alta (que duram minutos).
2. **Às vezes sai uma notícia.** Uma manchete muda `V` de uma vez, para cima ou para baixo.
3. **Os agentes decidem.** Cada agente olha o mercado do segundo anterior e envia ordens. A ordem
   em que eles agem é sorteada a cada tick, para ninguém ter vantagem fixa de chegar primeiro.
4. **O controle de risco filtra.** Ordens que estouram o limite de posição de um agente são barradas.
5. **O livro de ofertas casa as ordens.** Vale a prioridade de preço e depois a de tempo. Cada
   negócio fechado atualiza a posição e o lucro dos dois lados.
6. **O mercado é publicado.** Melhor compra, melhor venda, profundidade e último preço ficam
   visíveis para o próximo tick.

O resultado é um mercado vivo: o preço oscila, o spread abre em momentos de estresse, notícias
geram saltos e cascatas de stop podem derrubar o preço por alguns minutos.

## Os agentes

| Agente | Quantos | O que faz | Vê o valor verdadeiro? |
|---|---|---|---|
| **Market maker** | 3 | Cota compra e venda o tempo todo e ganha a diferença (o spread). Não sabe o valor verdadeiro: deduz pelo fluxo de ordens (muita compra → sobe a cotação) e pelas notícias públicas | Não |
| **Informado** | 2 | Sabe o valor verdadeiro (com um pouco de ruído). Quando o preço está longe dele, monta uma posição e, ao negociar, empurra o preço de volta | Sim |
| **Noise trader** | 25 | Compra e vende sem informação, como o investidor de varejo. Gera boa parte do volume e é quem paga a conta dos outros | Não |
| **Institucional** | 2 | Um fundo que precisa executar ordens grandes. Fatia cada ordem em pedaços menores ao longo do tempo para não mover demais o preço | Não |
| **Momentum** | 3 | Segue tendência de curto prazo (cruzamento de médias móveis de segundos), com stop pela volatilidade recente | Não |
| **Mean-reverter** | 2 | Aposta que o preço volta à média quando se afasta muito | Não |
| **Reator de notícia** | 8 | Reage às manchetes e segura a posição por 5 minutos a 1 hora | Não |
| **Stop-loss** | 6 | Monta uma posição e zera tudo quando a perda passa de 1,5–4%; depois de um tempo monta outra. 70% das posições são compradas, então uma queda dispara mais stops que uma alta, e os stops em sequência geram cascatas de queda | Não |
| **Ordem limitada** | 20 | Deixa ordens paradas no livro perto do preço, sem nunca cruzar. Mistura visão do valor (com erro), tendência e ruído | Sim, com erro |

A chegada de ordens de noise traders, institucionais e ordens limitadas segue a forma de **U** dos
mercados reais: é maior na abertura e no fechamento e menor no meio do dia. Essa forma é um dado
de entrada do modelo, não um resultado.

## Quem ganha e quem perde

O dinheiro do simulador é fechado: o que um agente ganha, outro perde, e a soma dá **zero exato**.
Números médios de 20 simulações independentes de 250 pregões (um ano de mercado cada), em milhares
de dólares:

| Agente | Lucro total | Por agente | Seeds com lucro | Por quê |
|---|---|---|---|---|
| **Informados** | **+1.383** | +692 | 20 de 20 | Sabem o valor verdadeiro e ganham de quem não sabe |
| **Market makers** | **+521** | +174 | 19 de 20 | Ganham o spread, mesmo perdendo às vezes para o informado |
| **Ordem limitada** | **+301** | +15 | 20 de 20 | Ganham o spread como pequenos market makers |
| Mean-reverter | +19 | +9 | 17 de 20 | Lucro pequeno |
| Stop-loss | −0,6 | −0,1 | 9 de 20 | Empata: negocia pouco |
| Reatores de notícia | −66 | −8 | 0 de 20 | O market maker já ajustou o preço antes de eles reagirem |
| Institucional | −250 | −125 | 0 de 20 | Paga pelo impacto de executar ordens grandes |
| Momentum | −386 | −129 | 0 de 20 | Entra atrasado na tendência e paga o spread toda vez que vira a posição |
| **Noise traders** | **−1.520** | −61 | 0 de 20 | Negociam sem informação: pagam o spread e perdem para o informado |

É o que a teoria de microestrutura prevê (Kyle 1985; Glosten-Milgrom 1985). Ganha quem tem
informação ou fornece liquidez. Paga quem negocia sem informação ou precisa executar com pressa.

## O que o simulador reproduz e o que não

Ele é comparado com os "fatos estilizados", padrões estatísticos que aparecem em praticamente
todo mercado real. Calibração e validação usam simulações diferentes, para que o ajuste não seja
avaliado com os mesmos dados que o orientaram.

**Reproduz:**
- caudas gordas: movimentos extremos são bem mais frequentes do que numa curva normal;
- retornos mais longos ficam mais próximos da curva normal, como no mercado real;
- volatilidade em grupos a 1 minuto (períodos agitados seguidos de agitados);
- volume e volatilidade andando juntos;
- spread de ~2,7 bp, que varia ao longo do pregão;
- negócios do mesmo lado em sequência no curto prazo (uma ordem grande fatiada vira várias compras
  seguidas);
- preço colado no valor verdadeiro (desvio típico de 0,3%), e um ano inteiro de pregões sem nenhuma
  simulação quebrar.

**Ainda não reproduz (medido e documentado):**
- o retorno de 1 minuto reverte um pouco; no mercado real ele é quase imprevisível;
- a volatilidade não tem a memória longa observada na prática;
- falta o efeito alavancagem (no mercado real, quedas aumentam a volatilidade);
- a sequência de negócios do mesmo lado some rápido demais (no real ela dura muito mais);
- não se confirma que quedas fortes sejam mais comuns que altas fortes, como no real (o teste sai
  inconclusivo);
- o preço demora alguns minutos para alcançar o valor verdadeiro (meia-vida de ~4,5 min);
- acontecem cerca de 0,3 flash crash por pregão, mais do que o normal.

Em números: passam cerca de 5 dos 10 fatos da bateria principal. Os detalhes estão em
[docs/STYLIZED_FACTS_VALIDATION.md](docs/STYLIZED_FACTS_VALIDATION.md).

## Como compilar e rodar

Linha de comando (simulação, calibração e testes):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIM_BUILD_UI=OFF
cmake --build build -j8
ctest --test-dir build
```

Janela gráfica (precisa de `brew install sfml@2`; no macOS compile com o clang da Apple):

```bash
cmake -S . -B build_ui -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang
cmake --build build_ui -j8 --target simulador
./build_ui/simulador
```

Exemplos:

```bash
# Uma simulação de 5 pregões, com relatório no final
./build/sim_headless --seed 42 --days 5 --output logs/run.bin

# Exporta o log para CSV
./build/analysis_csv_export logs/run.bin out/

# 10 simulações de 60 pregões em paralelo, com estatística entre elas
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir /tmp/cal
```

A mesma seed sempre produz exatamente o mesmo resultado, num mesmo compilador. Os números deste
README vêm do build com g++ (GCC 14); o clang dá outra trajetória para a mesma seed.

## Documentação técnica

- [docs/ARCHITECTURE_OVERVIEW.md](docs/ARCHITECTURE_OVERVIEW.md): camadas do código, ordem do tick,
  detalhes de cada agente, processo fundamental, formato do log e como adicionar um agente novo.
- [docs/STYLIZED_FACTS_VALIDATION.md](docs/STYLIZED_FACTS_VALIDATION.md): o protocolo de
  calibração e validação, cada fato estilizado, os resultados e as falhas medidas.
- Os parâmetros do modelo ficam em `src/core/Config.h`.
