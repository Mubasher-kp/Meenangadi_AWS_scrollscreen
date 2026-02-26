# ESP32 Weather Forecast — Device 1020

Personal reference for the ESP32 sketch that pulls hourly forecast
from the Indra AWS API and prints it to Serial Monitor.

---

## Wiring
Nothing — USB only. Serial Monitor at **115200 baud**.

---

## Credentials

```cpp
SSID        : "FTTH-12E8"
PASSWORD    : "48@97166@"
API URL     : https://indraawsapi.misteo.co/weather/api/v1/forecast?device_id=1020
TOKEN       : Bearer f7AmejFHa302cUn3aczf7vVlMhzDMyPZu4N9Vl1woHlwdAbBSySCVPFpgtBWbrjAX
              LxFbBSXtQmZNxfAUgapqOkHO6GjFkdmi6IbCHitibM6RRyVw6WwWJ7UCDAPyDEV
DEVICE ID   : 1020
```

---

## Libraries Needed

| Library | Install via |
|---------|-------------|
| ArduinoJson | Arduino Library Manager |
| WiFi | Built-in ESP32 core |
| WiFiClientSecure | Built-in ESP32 core |
| HTTPClient | Built-in ESP32 core |

---

## How It Works

- Connects WiFi → syncs IST time via NTP (`pool.ntp.org`, UTC+5:30)
- Fetches full forecast JSON once on boot
- Every 5 seconds → finds the entry matching current IST hour → prints it
- When hour changes → re-fetches from API automatically

---

## API Response Keys (Device 1020)

| Dashboard Label | JSON Key | Type |
|----------------|----------|------|
| Rain Fall | `rainfall` | float (mm) |
| Temperature | `temperature` | float (°C) |
| Humidity | `humidity` / `hum` / `rh` | float (%) |
| Pressure | `pressure` | float (hPa) |
| Wind Speed | `wind_speed` | float (m/s) |
| Direction | `wind_direction` | float (degrees) |
| Time | `time` | string ISO8601 |

> Humidity key was unclear at time of writing — DEBUG dump on
> first boot prints all keys. Check Serial Monitor output.

---

## NTP Settings

```cpp
NTP_SERVER  = "pool.ntp.org"
UTC_OFFSET  = 19800   // IST = UTC + 5:30 (5*3600 + 30*60)
DST_OFFSET  = 0
```

---

## Known Issues

- `wind_direction` comes as **float degrees**, not string — converted
  to compass using 16-point division `(deg + 11.25) / 22.5`
- `humidity` returns `null` from API sometimes — code tries
  `humidity`, `hum`, `relative_humidity`, `rh` in order
- `WiFiClientSecure` needs its own `#include <WiFiClientSecure.h>` —
  not pulled in by `WiFi.h`
- `printf("%s", nullptr)` crashes ESP32 (LoadProhibited) —
  always use `safeStr()` for string fields

---

## Bugs Fixed During Development

1. **Missing `#include <WiFiClientSecure.h>`** → compile error
2. **Null pointer crash** on `wind_direction` → added `safeStr()` helper
3. **Wind direction showed N/A** → API sends degrees (float), not string
4. **Humidity missing** → API key name mismatch, added multi-key fallback

---

## Serial Output Sample

```
============================================
   FORECAST  17:00 IST  |  Now: 17:03:01
============================================
  Forecast Time  : 2026-02-26T17:00:00
--------------------------------------------
  Rain Fall      : 0.17 mm
  Temperature    : 25.21 C
  Humidity       : 61.00%
  Pressure       : 923.01 hPa
  Wind Speed     : 2.50 m/s
  Direction      : 256 deg (WSW)
============================================
```

---

## Dashboard Reference

URL : https://carbonneutral.deepflow.in/forecast
Data matches device_id=1020 on the Indra AWS API.
