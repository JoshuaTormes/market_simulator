import pandas as pd
import mplfinance as mpf

df = pd.read_csv("candles.csv")

df = df[["index", "open", "high", "low", "close", "volume"]]

df = df.dropna(subset=["open", "high", "low", "close"])

df["date"] = pd.to_datetime(df["index"], unit="s")
df = df.set_index("date")

df = df.sort_index()

mpf.plot(
    df,
    type="candle",
    volume=True,
    style="charles",
    title="Simulação de Mercado",
    ylabel="Preço",
    ylabel_lower="Volume"
)
