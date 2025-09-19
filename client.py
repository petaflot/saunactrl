#!/usr/bin/env python3
import asyncio
import websockets
import sys

ESP_HOST = "192.168.14.123"   # replace with your ESP's IP
ESP_PORT = 80
WS_PATH = "/ws"

async def sender(ws):
    """Read stdin and send lines to the WebSocket."""
    loop = asyncio.get_event_loop()
    while True:
        # run input() in a thread so it doesn’t block the event loop
        msg = await loop.run_in_executor(None, sys.stdin.readline)
        msg = msg.strip()
        if not msg:
            continue
        await ws.send(msg)
        print(f"Sent: {msg}")

async def receiver(ws):
    """Receive and print messages from the WebSocket."""
    try:
        async for message in ws:
            print(f"Received: {message}")
    except websockets.ConnectionClosed:
        print("Connection closed by server")

async def main():
    uri = f"ws://{ESP_HOST}:{ESP_PORT}{WS_PATH}"
    async with websockets.connect(uri) as ws:
        print(f"Connected to {uri}")
        # run sender and receiver concurrently
        await asyncio.gather(
            sender(ws),
            receiver(ws)
        )

if __name__ == "__main__":
    asyncio.run(main())

