# Market Microstructure Simulator

A stock market simulator written in C++17. It replays a trading session second by second, with a
real limit order book and 71 agents running different strategies.

The core idea: **nobody writes the price.** The company has a "true" value that changes over time
and with news, but that value stays hidden. The traded price comes only from buy and sell orders
meeting in the book, and what gets measured is whether the market discovers the true value on its
own.

## How it works

Each **tick** is 1 second of trading. A session has 23,400 ticks (6.5 h), and a year has 252
sessions. Every tick runs the same fixed sequence:

1. **The fundamental value moves.** The company's true value (`V`) changes a little. It switches
   between calm phases (lasting hours) and high-volatility phases (lasting minutes).
2. **Sometimes a news item arrives.** A headline moves `V` at once, up or down.
3. **The agents decide.** Each agent looks at the market as of the previous second and sends
   orders. The order in which they act is reshuffled every tick, so nobody gets a fixed
   first-arrival advantage.
4. **The risk gate filters.** Orders that would break an agent's position limit are blocked.
5. **The order book matches.** Price priority first, then time priority. Each fill updates the
   position and P&L of both sides.
6. **The market is published.** Best bid, best ask, depth and last price become visible for the
   next tick.

The result is a live market: the price fluctuates, news causes jumps, and stop cascades can knock
the price down for a few minutes.

## The agents

| Agent | Count | What it does | Sees the true value? |
|---|---|---|---|
| **Market maker** | 3 | Quotes both sides all the time and earns the spread. Does not know the true value: infers it from order flow (heavy buying → raise the quote) and from public news | No |
| **Informed trader** | 2 | Knows the true value (with some noise). When the price is far from it, builds a position, and its trading pushes the price back | Yes |
| **Noise trader** | 25 | Buys and sells without information, like a retail investor. Generates much of the volume and ends up paying everyone else | No |
| **Institutional** | 2 | A fund that has to execute large orders. Slices each order into smaller pieces over time so as not to move the price too much | No |
| **Momentum** | 3 | Follows short-term trends (moving-average crossover over seconds), with a stop based on recent volatility | No |
| **Mean reverter** | 2 | Bets that the price returns to its average after straying too far | No |
| **News reactor** | 8 | Reacts to headlines and holds the position for 5 minutes to 1 hour | No |
| **Stop-loss** | 6 | Opens a position and closes it entirely when the loss exceeds 1.5–4%; after a while it opens another. 70% of positions are long, so a drop triggers more stops than a rise, and stops in a row produce downward cascades | No |
| **Limit order trader** | 20 | Leaves resting orders in the book near the price and never crosses. Blends a view of the value (with error), trend and noise | Yes, with error |

Order arrival for noise traders, institutionals and limit order traders follows the **U shape**
of real markets: higher at the open and close, lower mid-day. That shape is a model input, not a
result.

## Who wins and who loses

Money in the simulator is closed: whatever one agent gains, another loses, and the total is
**exactly zero**. Averages over 20 independent runs of 250 sessions (one market year each), in
thousands of US dollars:

| Agent | Total P&L | Per agent | Runs in profit | Why |
|---|---|---|---|---|
| **Informed traders** | **+1,383** | +692 | 20 of 20 | They know the true value and win against those who don't |
| **Market makers** | **+521** | +174 | 19 of 20 | They earn the spread, even though they sometimes lose to the informed |
| **Limit order traders** | **+301** | +15 | 20 of 20 | They earn the spread as small market makers |
| Mean reverters | +19 | +9 | 17 of 20 | Small profit |
| Stop-loss | −0.6 | −0.1 | 9 of 20 | Break even: they trade little |
| News reactors | −66 | −8 | 0 of 20 | The market maker has already repriced before they react |
| Institutional | −250 | −125 | 0 of 20 | They pay the impact of executing large orders |
| Momentum | −386 | −129 | 0 of 20 | They enter trends late and pay the spread every time they flip |
| **Noise traders** | **−1,520** | −61 | 0 of 20 | They trade without information: they pay the spread and lose to the informed |

This is what microstructure theory predicts (Kyle 1985; Glosten-Milgrom 1985). Those who have
information or provide liquidity win. Those who trade without information or need to execute in a
hurry pay.

## What the simulator reproduces and what it doesn't

It is checked against the "stylized facts", statistical patterns found in nearly every real
market. Calibration and validation use different runs, so a tuning choice is never judged on the
same data that guided it.

**Reproduces:**
- fat tails: extreme moves are far more frequent than under a normal curve;
- longer-horizon returns get closer to a normal curve, as in real markets;
- volatility clustering at 1 minute (turbulent periods followed by turbulent periods);
- volume and volatility moving together;
- a spread of ~2.7 bp that varies over the session;
- short-run runs of same-side trades (a sliced large order becomes several buys in a row);
- a price that stays close to the true value (typical deviation 0.3%), and a full year of sessions
  without any run breaking down.

**Does not reproduce yet (measured and documented):**
- the 1-minute return reverts slightly; in real markets it is nearly unpredictable;
- volatility lacks the long memory seen in practice;
- the leverage effect is missing (in real markets, drops raise volatility);
- runs of same-side trades fade too fast (in real markets they last much longer);
- it does not confirm that sharp drops are more common than sharp rises, as in real markets (the
  test is inconclusive);
- the price takes a few minutes to catch up with the true value (half-life ~4.5 min);
- there are about 0.3 flash crashes per session, more than normal.

In numbers: about 5 of the 10 facts in the main battery pass. Details are in
[docs/STYLIZED_FACTS_VALIDATION.md](docs/STYLIZED_FACTS_VALIDATION.md) (in Portuguese).

## Build and run

Command line (simulation, calibration and tests):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIM_BUILD_UI=OFF
cmake --build build -j8
ctest --test-dir build
```

Graphical window (needs `brew install sfml@2`; on macOS build with Apple clang):

```bash
cmake -S . -B build_ui -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang
cmake --build build_ui -j8 --target simulador
./build_ui/simulador
```

Examples:

```bash
# One 5-session run, with a report at the end
./build/sim_headless --seed 42 --days 5 --output logs/run.bin

# Export the log to CSV
./build/analysis_csv_export logs/run.bin out/

# 10 runs of 60 sessions in parallel, with statistics across them
./build/sim_calibrate --seed-set cal --days 60 --jobs 8 --outdir /tmp/cal
```

The same seed always produces exactly the same result with the same compiler. The numbers in this
README come from the g++ build (GCC 14); clang gives a different path for the same seed.

## Technical documentation (in Portuguese)

- [docs/ARCHITECTURE_OVERVIEW.md](docs/ARCHITECTURE_OVERVIEW.md): code layers, tick order, details of
  each agent, the fundamental process, the log format and how to add a new agent.
- [docs/STYLIZED_FACTS_VALIDATION.md](docs/STYLIZED_FACTS_VALIDATION.md): the calibration and
  validation protocol, each stylized fact, the results and the measured failures.
- Model parameters live in `src/core/Config.h`.
