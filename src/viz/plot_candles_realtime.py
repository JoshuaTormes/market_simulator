import time
import pandas as pd
import mplfinance as mpf
import matplotlib.pyplot as plt

plt.ion()

fig = None
ax_price = None
ax_volume = None
last_len = 0

while True:
    try:
        df = pd.read_csv("candles.csv")

        df = df[["index", "open", "high", "low", "close", "volume"]]
        df = df.dropna(subset=["open", "high", "low", "close"])

        if df.empty:
            time.sleep(0.5)
            continue

        df["date"] = pd.to_datetime(df["index"], unit="s")
        df = df.set_index("date")
        df = df.sort_index()

        if len(df) == last_len:
            time.sleep(0.5)
            continue

        last_len = len(df)

        if fig is None:
            fig = plt.figure(figsize=(12, 8))
            ax_price = fig.add_subplot(2, 1, 1)
            ax_volume = fig.add_subplot(2, 1, 2, sharex=ax_price)

        ax_price.clear()
        ax_volume.clear()

        mpf.plot(
            df,
            type="candle",
            ax=ax_price,
            volume=ax_volume,
            style="charles",
            show_nontrading=False
        )

        plt.pause(0.01)

    except Exception as e:
        print("Erro:", e)
        time.sleep(1)
