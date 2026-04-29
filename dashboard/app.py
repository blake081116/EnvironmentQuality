import asyncio
import json
import math
import os
import random
import time
from pathlib import Path
from threading import Event, Lock
from typing import Any

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from serial import Serial
from serial.tools import list_ports


BASE_DIR = Path(__file__).resolve().parent
STATIC_DIR = BASE_DIR / "static"

app = FastAPI(title="Environment Quality Dashboard")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.middleware("http")
async def add_no_cache_headers(request: Request, call_next):
    response = await call_next(request)
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

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
    pm2p5 = max(1.0, 8.0 + wave * 2.5 + random.uniform(-0.6, 0.6))
    raw_temp = 25.1 + wave * 0.8 + random.uniform(-0.1, 0.1)
    comp_temp = raw_temp - 0.6
    raw_humidity = 48.0 - wave * 3.2 + random.uniform(-0.4, 0.4)
    comp_humidity = raw_humidity - 1.4

    return {
        "timestamp": now,
        "source": "demo",
        "connected": False,
        "ieq": {
            "score": 100,
        },
        "bme": {
            "temp": comp_temp,
            "rawTemp": raw_temp,
            "humidity": comp_humidity,
            "rawHumidity": raw_humidity,
            "pressure": 1012.5 + math.sin(now / 45.0) * 2.0,
            "iaq": iaq,
            "staticIaq": iaq * 0.92,
            "accuracy": 0,
            "eco2": 500.0 + max(0.0, iaq - 50.0) * 7.0,
            "bvoc": 0.25 + max(0.0, iaq - 50.0) * 0.012,
            "gas": 85000.0 - max(0.0, iaq - 50.0) * 700.0,
            "rawGas": 92000.0 - max(0.0, iaq - 50.0) * 650.0,
            "compGas": 85000.0 - max(0.0, iaq - 50.0) * 700.0,
            "gasPercentage": 74.0 - max(0.0, iaq - 50.0) * 0.35,
            "stabilizationStatus": 1,
        },
        "sound": {
            "db": sound_db,
            "vrms": 0.006 + max(0.0, sound_db - 45.0) * 0.00035,
        },
        "sps30": {
            "ready": True,
            "error": 0,
            "pm1p0": pm2p5 * 0.64,
            "pm2p5": pm2p5,
            "pm4p0": pm2p5 * 1.18,
            "pm10p0": pm2p5 * 1.42,
            "nc0p5": pm2p5 * 78.0,
            "nc1p0": pm2p5 * 28.0,
            "nc2p5": pm2p5 * 5.4,
            "nc4p0": pm2p5 * 1.5,
            "nc10p0": pm2p5 * 0.35,
            "typicalSize": 0.62 + wave * 0.04,
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
    sample.setdefault("sps30", {})
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
def root() -> FileResponse:
    return dashboard_file()


@app.get("/dashboard")
def index() -> FileResponse:
    return dashboard_file()


def dashboard_file() -> FileResponse:
    return FileResponse(
        STATIC_DIR / "index.html",
        headers={
            "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
            "Pragma": "no-cache",
            "Expires": "0",
        },
    )


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
