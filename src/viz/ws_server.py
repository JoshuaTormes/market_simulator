import asyncio
import websockets
import json
import pandas as pd
import os

conexoes = set()
CANDLES_FILE = "candles.csv"
PNL_FILE = "agents_pnl.csv"  # CSV gerado pelo Engine
last_candle_sent = 0
last_pnl_sent = 0
current_candle_size = 0
current_pnl_size = 0

async def enviar(dado, ws=None):
    if ws:
        await ws.send(json.dumps(dado))
    elif conexoes:
        await asyncio.gather(
            *(c.send(json.dumps(dado)) for c in list(conexoes)),
            return_exceptions=True
        )

async def enviar_historico(websocket):
    # --- Velas ---
    if os.path.exists(CANDLES_FILE):
        try:
            df = pd.read_csv(CANDLES_FILE).dropna()
            for _, r in df.iterrows():
                payload = {
                    "type": "candle",
                    "t": int(r["startTick"]),
                    "o": float(r["open"]),
                    "h": float(r["high"]),
                    "l": float(r["low"]),
                    "c": float(r["close"]),
                    "v": int(float(r["volume"]))
                }
                await enviar(payload, ws=websocket)
                await asyncio.sleep(0.001)
        except Exception:
            pass

    # --- PnL ---
    if os.path.exists(PNL_FILE):
        try:
            df = pd.read_csv(PNL_FILE, dtype={
                "tick": int,
                "agentId": int,
                "agentType": str,
                "pnl": float
            }).dropna()
            for _, r in df.iterrows():
                payload = {
                    "type": "pnl",
                    "tick": int(r["tick"]),
                    "agentId": int(r["agentId"]),
                    "agentType": r["agentType"],
                    "pnl": float(r["pnl"])
                }
                await enviar(payload, ws=websocket)
                await asyncio.sleep(0.001)
        except Exception:
            pass

async def handler(websocket):
    await enviar_historico(websocket)
    conexoes.add(websocket)
    print(f"Novo cliente conectado. Total: {len(conexoes)}")
    try:
        async for _ in websocket:
            pass
    finally:
        conexoes.remove(websocket)
        print(f"Cliente desconectado. Total: {len(conexoes)}")

async def produtor():
    global last_candle_sent, last_pnl_sent
    global current_candle_size, current_pnl_size

    while True:
        # --- Velas ---
        if os.path.exists(CANDLES_FILE):
            new_size = os.path.getsize(CANDLES_FILE)
            if new_size < current_candle_size:
                last_candle_sent = 0
            current_candle_size = new_size

            try:
                df = pd.read_csv(CANDLES_FILE).dropna()
                if len(df) < last_candle_sent:
                    last_candle_sent = 0
                if len(df) > last_candle_sent:
                    novos = df.iloc[last_candle_sent:]
                    for _, r in novos.iterrows():
                        payload = {
                            "type": "candle",
                            "t": int(r["startTick"]),
                            "o": float(r["open"]),
                            "h": float(r["high"]),
                            "l": float(r["low"]),
                            "c": float(r["close"]),
                            "v": int(float(r["volume"]))
                        }
                        await enviar(payload)
                        await asyncio.sleep(0.001)
                    last_candle_sent = len(df)
            except Exception:
                pass

        # --- PnL ---
        if os.path.exists(PNL_FILE):
            new_size = os.path.getsize(PNL_FILE)
            if new_size < current_pnl_size:
                last_pnl_sent = 0
            current_pnl_size = new_size

            try:
                df = pd.read_csv(PNL_FILE, dtype={
                    "tick": int,
                    "agentId": int,
                    "agentType": str,
                    "pnl": float
                }).dropna()
                if len(df) < last_pnl_sent:
                    last_pnl_sent = 0
                if len(df) > last_pnl_sent:
                    novos = df.iloc[last_pnl_sent:]
                    for _, r in novos.iterrows():
                        payload = {
                            "type": "pnl",
                            "tick": int(r["tick"]),
                            "agentId": int(r["agentId"]),
                            "agentType": r["agentType"],
                            "pnl": float(r["pnl"])
                        }
                        await enviar(payload)
                        await asyncio.sleep(0.001)
                    last_pnl_sent = len(df)
            except Exception:
                pass

        await asyncio.sleep(0.01)

async def iniciar_ws():
    async with websockets.serve(handler, "localhost", 8765):
        print("WebSocket em ws://localhost:8765")
        await produtor()

asyncio.run(iniciar_ws())
