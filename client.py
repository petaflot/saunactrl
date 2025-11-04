#!/usr/bin/env python3
# vim: noet ts=4 number
import asyncio
import websockets
import sys, time
import json, hmac, hashlib

ESP_PORT = 80
#ESP_PORT = 1560
WS_PATH = "/ws"

RECONNECT_DELAY = 5  # seconds between retries

def compute_hmac(json_str, secret):
	"""Compute hex-encoded HMAC-SHA256."""
	return hmac.new(secret.encode(), json_str.encode(), hashlib.sha256).hexdigest()

def add_hmac(data, secret):
	"""Return a new dict with an 'hmac' field added."""
	# Make canonical JSON *without* hmac
	hmac_hex = compute_hmac(data, secret)
	return data[:-1] + f',"hmac:{hmac_hex}"' + ']'

async def sender(ws, secret):
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
				if secret:	json_data = add_hmac(json.dumps(msg.split(','), sort_keys = True, separators = (',', ':')), secret)
				else:		json_data = json.dumps(msg, separators = (',', ':'))
				await ws.send(json_data)
				print(f"Sent: {json_data}")
	except asyncio.CancelledError:
		return

async def receiver(ws):
	"""Receive and print messages from the WebSocket."""
	try:
		async for message in ws:
			print(f"Received: {message}")
	except websockets.ConnectionClosed:
		print("Connection closed by server")

async def session(host, port, secret):
	"""Single connection session."""
	uri = f"ws://{host}:{port}{WS_PATH}"
	async with websockets.connect(uri) as ws:
		print(f"Connected to {uri}")
		send_task = asyncio.create_task(sender(ws, secret))
		recv_task = asyncio.create_task(receiver(ws))

		done, pending = await asyncio.wait(
			[send_task, recv_task],
			return_when=asyncio.FIRST_COMPLETED
		)
		for task in pending:
			task.cancel()

async def main(host, port, secret = None):
	while True:
		try:
			await session(host, port, secret)
		except (OSError, websockets.InvalidURI, websockets.InvalidHandshake) as e:
			print(f"Connection failed: {e}")
		except websockets.ConnectionClosedError:
			print("Connection closed unexpectedly")
		# Wait before retry
		print(f"Reconnecting in {RECONNECT_DELAY}s...")
		await asyncio.sleep(RECONNECT_DELAY)

if __name__ == "__main__":
	from sys import argv

	secret = "my_secret_seed"

	try:
		try:
			asyncio.run(main(argv[1], argv[2], secret))
		except IndexError:
			asyncio.run(main(argv[1], ESP_PORT, secret))
	except KeyboardInterrupt:
		print("\nInterrupted by user")

