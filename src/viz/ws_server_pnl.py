import asyncio
import websockets
import json
import pandas as pd
import os

conexoes = set()
PNL_FILE = "agents_pnl.csv"
last_sent = 0
current_file_size = 0

async def enviar(dado, ws=None):
    if ws:
        await ws.send(json.dumps(dado))
    elif conexoes:
        await asyncio.gather(
            *(c.send(json.dumps(dado)) for c in list(conexoes)),
            return_exceptions=True
        )

async def enviar_historico(websocket):
    if not os.path.exists(PNL_FILE):
        return

    df = pd.read_csv(PNL_FILE)
    if df.empty:
        return

    for _, r in df.iterrows():
        payload = {
            "tick": int(r["tick"]),
            "agentId": int(r["agentId"]),
            "agentType": r["agentType"],
            "pnl": float(r["pnl"])
        }
        await enviar(payload, ws=websocket)
        await asyncio.sleep(0.005)

async def handler(websocket):
    await enviar_historico(websocket)
    conexoes.add(websocket)
    print(f"Novo cliente PnL conectado. Total: {len(conexoes)}")
    
    try:
        async for _ in websocket:
            pass
    finally:
        conexoes.remove(websocket)
        print(f"Cliente PnL desconectado. Total: {len(conexoes)}")

async def produtor():
    global last_sent
    global current_file_size

    while True:
        if not os.path.exists(PNL_FILE):
            await asyncio.sleep(0.01)
            continue

        new_file_size = os.path.getsize(PNL_FILE)
        if new_file_size < current_file_size:
            last_sent = 0
        current_file_size = new_file_size

        df = pd.read_csv(PNL_FILE)
        if df.empty:
            await asyncio.sleep(0.01)
            continue

        if len(df) > last_sent:
            novos = df.iloc[last_sent:]
            for _, r in novos.iterrows():
                payload = {
                    "tick": int(r["tick"]),
                    "agentId": int(r["agentId"]),
                    "agentType": r["agentType"],
                    "pnl": float(r["pnl"])
                }
                await enviar(payload)
                await asyncio.sleep(0.01)
            last_sent = len(df)

        await asyncio.sleep(0.01)

async def iniciar_ws():
    async with websockets.serve(handler, "localhost", 8766):  # Porta diferente
        print("WebSocket PnL em ws://localhost:8766")
        await produtor()

asyncio.run(iniciar_ws())
