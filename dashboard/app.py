import asyncio
import json
import math
import os
import random
import time
from pathlib import Path
from threading import Event, Lock
from typing import Any

from fastapi import FastAPI
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from serial import Serial
from serial.tools import list_ports


BASE_DIR = Path(__file__).resolve().parent
STATIC_DIR = BASE_DIR / "static"

app = FastAPI(title="Environment Quality Dashboard")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

state_lock = Lock()
latest_sample: dict[str, Any] | None = None
serial_task: asyncio.Task | None = None
serial_stop_event = Event()


def make_demo_sample() -> dict[str, Any]:
    now = time.time()
    wave = math.sin(now / 18.0)
    faster_wave = math.sin(now / 5.0)

    iaq = 50.0 + wave * 8.0 + random.uniform(-1.5, 1.5)
    sound_db = 50.0 + faster_wave * 4.0 + random.uniform(-1.0, 1.0)

    return {
        "timestamp": now,
        "source": "demo",
        "connected": False,
        "ieq": {
            "score": 100,
        },
        "bme": {
            "temp": 24.5 + wave * 0.9 + random.uniform(-0.1, 0.1),
            "humidity": 46.0 - wave * 3.0 + random.uniform(-0.4, 0.4),
            "pressure": 1012.5 + math.sin(now / 45.0) * 2.0,
            "iaq": iaq,
            "accuracy": 0,
            "eco2": 500.0 + max(0.0, iaq - 50.0) * 7.0,
            "bvoc": 0.25 + max(0.0, iaq - 50.0) * 0.012,
            "gas": 85000.0 - max(0.0, iaq - 50.0) * 700.0,
        },
        "sound": {
            "db": sound_db,
            "vrms": 0.006 + max(0.0, sound_db - 45.0) * 0.00035,
        },
    }


def get_latest_sample() -> dict[str, Any]:
    with state_lock:
        if latest_sample is not None:
            return latest_sample

    return make_demo_sample()


def normalize_device_sample(sample: dict[str, Any]) -> dict[str, Any]:
    sample.setdefault("timestamp", time.time())
    sample.setdefault("source", "serial")
    sample.setdefault("connected", True)
    sample.setdefault("ieq", {})
    sample.setdefault("bme", {})
    sample.setdefault("sound", {})
    return sample


def save_latest_sample(sample: dict[str, Any]) -> None:
    with state_lock:
        global latest_sample
        latest_sample = normalize_device_sample(sample)


def find_serial_port() -> str | None:
    ports = list(list_ports.comports())
    preferred_names = ("usbmodem", "usbserial", "wchusbserial")

    for port in ports:
        device = port.device.lower()
        description = port.description.lower()
        if any(name in device or name in description for name in preferred_names):
            return port.device

    return None


def read_serial_forever(port: str, baud: int) -> None:
    with Serial(port, baudrate=baud, timeout=1) as connection:
        while not serial_stop_event.is_set():
            raw_line = connection.readline()
            if not raw_line:
                continue

            try:
                line = raw_line.decode("utf-8", errors="ignore").strip()
                if not line.startswith("{"):
                    continue

                save_latest_sample(json.loads(line))
            except json.JSONDecodeError:
                continue


async def serial_reader_loop() -> None:
    baud = int(os.getenv("SERIAL_BAUD", "115200"))
    configured_port = os.getenv("SERIAL_PORT", "").strip()

    while True:
        port = find_serial_port() if configured_port.lower() == "auto" else configured_port

        if not port:
            if configured_port.lower() == "auto":
                print("No serial port found. Retrying in 3 seconds.")
                await asyncio.sleep(3)
                continue

            print("SERIAL_PORT is not set. Using demo data.")
            return

        try:
            print(f"Reading Nano ESP32 serial data from {port} at {baud} baud.")
            await asyncio.to_thread(read_serial_forever, port, baud)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            print(f"Serial read failed on {port}: {exc}. Retrying in 3 seconds.")
            await asyncio.sleep(3)


@app.on_event("startup")
async def startup() -> None:
    global serial_task

    if os.getenv("SERIAL_PORT"):
        serial_stop_event.clear()
        serial_task = asyncio.create_task(serial_reader_loop())


@app.on_event("shutdown")
async def shutdown() -> None:
    serial_stop_event.set()

    if serial_task:
        serial_task.cancel()


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/api/latest")
def latest() -> dict[str, Any]:
    return get_latest_sample()


@app.get("/api/ports")
def ports() -> list[dict[str, str]]:
    return [
        {
            "device": port.device,
            "description": port.description,
        }
        for port in list_ports.comports()
    ]


@app.post("/api/push")
def push_sample(sample: dict[str, Any]) -> dict[str, str]:
    sample["source"] = sample.get("source", "device")
    save_latest_sample(sample)

    return {"status": "ok"}


@app.get("/api/stream")
async def stream() -> StreamingResponse:
    async def event_generator():
        while True:
            payload = json.dumps(get_latest_sample())
            yield f"data: {payload}\n\n"
            await asyncio.sleep(1)

    return StreamingResponse(event_generator(), media_type="text/event-stream")
