# EnvironmentQuality

Arduino Nano ESP32 indoor environment monitor with BME688, MAX4466, SPS30, OLED display, and a FastAPI dashboard.

## Dashboard

The dashboard lives in the `dashboard` directory and is built with FastAPI.

### Environment Setup

Option 1: use `pip`.

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r dashboard/requirements.txt
```

Option 2: use `conda`.

```bash
conda env create -f environment.yml
conda activate csci561
```

### Run With Demo Data

If no device is connected, run the dashboard in demo mode:

```bash
uvicorn dashboard.app:app --reload
```

Open:

```text
http://127.0.0.1:8000
```

### Run With Device Data

#### Option 1: Wireless Mode

Use this mode when the Nano ESP32 is powered separately and is not connected to your Mac over USB.

In `main.ino/main.ino.ino`, set your Wi-Fi credentials and dashboard URL:

```cpp
const char* WIFI_SSID = "Your_WiFi_Name";
const char* WIFI_PASSWORD = "Your_WiFi_Password";
const char* API_URL = "http://YOUR_MAC_IP:8000/api/push";
```

Find your Mac Wi-Fi IP address:

```bash
ipconfig getifaddr en0
```

For example, if the command returns `192.168.1.25`, set:

```cpp
const char* API_URL = "http://192.168.1.25:8000/api/push";
```

Start the dashboard so it accepts requests from devices on the same Wi-Fi network:

```bash
uvicorn dashboard.app:app --host 0.0.0.0 --port 8000 --reload
```

Open on your Mac:

```text
http://127.0.0.1:8000
```

Or open from another device on the same network:

```text
http://YOUR_MAC_IP:8000
```

Wireless mode notes:

- The Nano ESP32 still needs power from a battery, USB power adapter, or another external source.
- The Mac and Nano ESP32 must be on the same Wi-Fi network.
- If the dashboard does not receive data, check that macOS Firewall allows Python/uvicorn to accept incoming connections.
- If your Mac IP changes, update `API_URL` and upload the firmware again.

#### Option 2: USB Serial Mode

Use this mode when the Arduino/Nano ESP32 is connected to your Mac over USB.

Automatically find the serial port:

```bash
SERIAL_PORT=auto uvicorn dashboard.app:app --reload
```

Or specify the port manually, for example on macOS:

```bash
SERIAL_PORT=/dev/cu.usbmodem206EF131BCA82 uvicorn dashboard.app:app --reload
```

The default serial baud rate is `115200`. To override it:

```bash
SERIAL_PORT=auto SERIAL_BAUD=115200 uvicorn dashboard.app:app --reload
```
