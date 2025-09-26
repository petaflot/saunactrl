#!/usr/bin/env python3
import asyncio
import websockets
import sys
import time

#ESP_HOST = "192.168.14.123"
ESP_HOST = "10.11.21.33"
ESP_PORT = 80
WS_PATH = "/ws"

RECONNECT_DELAY = 5  # seconds between retries

async def sender(ws):
    """Read stdin and send lines to the WebSocket."""
    loop = asyncio.get_event_loop()
    try:
        while True:
            msg = await loop.run_in_executor(None, sys.stdin.readline)
            if not msg:  # EOF (Ctrl+D)
                break
            msg = msg.strip()
            if msg == '?':
                print("""Available commands (set):
- enable
- disable
- target:<float temperature>
- relay:<int>:["on"|"off"|"pid"]
Available commands (query):
- enabled
- ambiant
- temp
- door
- relays
""")
            elif msg:
                await ws.send(msg)
                print(f"Sent: {msg}")
    except asyncio.CancelledError:
        return

async def receiver(ws):
    """Receive and print messages from the WebSocket."""
    try:
        async for message in ws:
            print(f"Received: {message}")
    except websockets.ConnectionClosed:
        print("Connection closed by server")

async def session():
    """Single connection session."""
    uri = f"ws://{ESP_HOST}:{ESP_PORT}{WS_PATH}"
    async with websockets.connect(uri) as ws:
        print(f"Connected to {uri}")
        send_task = asyncio.create_task(sender(ws))
        recv_task = asyncio.create_task(receiver(ws))

        done, pending = await asyncio.wait(
            [send_task, recv_task],
            return_when=asyncio.FIRST_COMPLETED
        )
        for task in pending:
            task.cancel()

async def main():
    while True:
        try:
            await session()
        except (OSError, websockets.InvalidURI, websockets.InvalidHandshake) as e:
            print(f"Connection failed: {e}")
        except websockets.ConnectionClosedError:
            print("Connection closed unexpectedly")
        # Wait before retry
        print(f"Reconnecting in {RECONNECT_DELAY}s...")
        await asyncio.sleep(RECONNECT_DELAY)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user")

