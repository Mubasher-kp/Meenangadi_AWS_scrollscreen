#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ── WiFi credentials ──────────────────────────────────────────────
const char* SSID     = "FTTH-12E8";
const char* PASSWORD = "48@97166@";

// ── API config ────────────────────────────────────────────────────
const char* API_URL = "https://indraawsapi.misteo.co/weather/api/v1/forecast?device_id=1020";
const char* AUTH_TOKEN =
  "Bearer f7AmejFHa302cUn3aczf7vVlMhzDMyPZu4N9Vl1woHlwdAbBSySCVPFpgtBWbrjAX"
  "LxFbBSXtQmZNxfAUgapqOkHO6GjFkdmi6IbCHitibM6RRyVw6WwWJ7UCDAPyDEV";

// ── NTP config (IST = UTC+5:30) ───────────────────────────────────
#define NTP_SERVER  "pool.ntp.org"
#define UTC_OFFSET  19800
#define DST_OFFSET  0

// ── Globals ───────────────────────────────────────────────────────
DynamicJsonDocument forecastDoc(16384);
bool forecastReady   = false;
int  lastFetchedHour = -1;

// ── Degrees to compass ────────────────────────────────────────────
const char* degreesToCompass(float deg) {
  const char* dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                        "S","SSW","SW","WSW","W","WNW","NW","NNW"};
  int idx = (int)((deg + 11.25f) / 22.5f) % 16;
  return dirs[idx];
}

// ── Safe string helper ────────────────────────────────────────────
const char* safeStr(JsonVariant v) {
  if (v.isNull()) return "N/A";
  if (!v.is<const char*>()) return "N/A";
  const char* s = v.as<const char*>();
  return s ? s : "N/A";
}

// ── Get float from multiple possible key names ────────────────────
float getFloat(JsonObject entry, const char* k1, const char* k2 = nullptr,
               const char* k3 = nullptr, const char* k4 = nullptr) {
  if (k1 && !entry[k1].isNull()) return entry[k1].as<float>();
  if (k2 && !entry[k2].isNull()) return entry[k2].as<float>();
  if (k3 && !entry[k3].isNull()) return entry[k3].as<float>();
  if (k4 && !entry[k4].isNull()) return entry[k4].as<float>();
  return -9999.0f;
}

// ── Get current IST hour ──────────────────────────────────────────
int getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -1;
  return timeinfo.tm_hour;
}

// ── Get current IST time string ───────────────────────────────────
String getCurrentTimeStr() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "??:??:??";
  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

// ── WiFi connect ──────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\nConnecting to WiFi: %s", SSID);
  WiFi.begin(SSID, PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++retries > 40) {
      Serial.println("\n[ERROR] WiFi timeout. Restarting...");
      ESP.restart();
    }
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── NTP sync ──────────────────────────────────────────────────────
void syncTime() {
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  Serial.print("Syncing NTP time");
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.print(".");
    if (++retries > 20) {
      Serial.println("\n[ERROR] NTP sync failed. Restarting...");
      ESP.restart();
    }
  }
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("\nIST Time synced: %s\n", buf);
}

// ── Fetch forecast from API ───────────────────────────────────────
void fetchForecast() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, API_URL);
  http.addHeader("Authorization", AUTH_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  Serial.println("\n[HTTP] Fetching forecast...");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    forecastDoc.clear();
    DeserializationError err = deserializeJson(forecastDoc, payload);
    if (err) {
      Serial.printf("[ERROR] JSON parse failed: %s\n", err.c_str());
      forecastReady = false;
    } else {
      Serial.println("[HTTP] Forecast cached OK.");
      forecastReady = true;

      // ── DEBUG: print all keys of first entry ─────────────────
      // Remove this block once humidity key is confirmed
      JsonArray arr;
      if      (!forecastDoc["forecast"].isNull()) arr = forecastDoc["forecast"].as<JsonArray>();
      else if (!forecastDoc["hourly"].isNull())   arr = forecastDoc["hourly"].as<JsonArray>();
      else if (!forecastDoc["data"].isNull())     arr = forecastDoc["data"].as<JsonArray>();
      if (!arr.isNull() && arr.size() > 0) {
        Serial.println("[DEBUG] Keys in first forecast entry:");
        for (JsonPair kv : arr[0].as<JsonObject>()) {
          Serial.printf("  key: %-25s value: ", kv.key().c_str());
          serializeJson(kv.value(), Serial);
          Serial.println();
        }
      }
      // ─────────────────────────────────────────────────────────
    }
  } else {
    Serial.printf("[ERROR] HTTP %d\n", httpCode);
    forecastReady = false;
  }
  http.end();
}

// ── Extract hour from "2026-02-26T16:00:00" ──────────────────────
int extractHour(const char* ts) {
  if (!ts || strlen(ts) < 16) return -1;
  char h[3] = { ts[11], ts[12], '\0' };
  return atoi(h);
}

// ── Display forecast for current hour ────────────────────────────
void displayCurrentHourForecast() {
  if (!forecastReady) {
    Serial.println("[WARN] No forecast data available.");
    return;
  }

  int currentHour = getCurrentHour();
  if (currentHour < 0) return;

  JsonArray arr;
  if      (!forecastDoc["forecast"].isNull()) arr = forecastDoc["forecast"].as<JsonArray>();
  else if (!forecastDoc["hourly"].isNull())   arr = forecastDoc["hourly"].as<JsonArray>();
  else if (!forecastDoc["data"].isNull())     arr = forecastDoc["data"].as<JsonArray>();

  if (arr.isNull() || arr.size() == 0) {
    Serial.println("[ERROR] Forecast array empty.");
    return;
  }

  bool found = false;
  for (JsonObject entry : arr) {
    const char* ts = nullptr;
    if      (entry.containsKey("time"))      ts = safeStr(entry["time"]);
    else if (entry.containsKey("datetime"))  ts = safeStr(entry["datetime"]);
    else if (entry.containsKey("timestamp")) ts = safeStr(entry["timestamp"]);

    if (extractHour(ts) == currentHour) {

      Serial.println("\n============================================");
      Serial.printf( "   FORECAST  %02d:00 IST  |  Now: %s\n",
                     currentHour, getCurrentTimeStr().c_str());
      Serial.println("============================================");
      Serial.printf( "  Forecast Time  : %s\n", ts);
      Serial.println("--------------------------------------------");

      // Rainfall
      float rain = getFloat(entry, "rainfall", "rain", "precipitation", "rain_mm");
      if (rain != -9999.0f)
        Serial.printf("  Rain Fall      : %.2f mm\n", rain);
      else
        Serial.println("  Rain Fall      : N/A");

      // Temperature
      float temp = getFloat(entry, "temperature", "temp", "air_temp", "temp_c");
      if (temp != -9999.0f)
        Serial.printf("  Temperature    : %.2f C\n", temp);
      else
        Serial.println("  Temperature    : N/A");

      // Humidity — tries all common key names
      float hum = getFloat(entry, "humidity", "hum", "relative_humidity", "rh");
      if (hum != -9999.0f)
        Serial.printf("  Humidity       : %.2f %%\n", hum);
      else
        Serial.println("  Humidity       : N/A");

      // Pressure
      float pres = getFloat(entry, "pressure", "pres", "air_pressure", "station_pressure");
      if (pres != -9999.0f)
        Serial.printf("  Pressure       : %.2f hPa\n", pres);
      else
        Serial.println("  Pressure       : N/A");

      // Wind Speed
      float wspd = getFloat(entry, "wind_speed", "windspeed", "wind_spd", "wspd");
      if (wspd != -9999.0f)
        Serial.printf("  Wind Speed     : %.2f m/s\n", wspd);
      else
        Serial.println("  Wind Speed     : N/A");

      // Wind Direction
      float wdir = getFloat(entry, "wind_direction", "wind_dir", "winddirection", "wdir");
      if (wdir != -9999.0f)
        Serial.printf("  Direction      : %.0f deg (%s)\n", wdir, degreesToCompass(wdir));
      else
        Serial.println("  Direction      : N/A");

      Serial.println("============================================");
      found = true;
      break;
    }
  }

  if (!found) {
    Serial.printf("[INFO] No entry for hour %02d. Re-fetching...\n", currentHour);
    fetchForecast();
    lastFetchedHour = -1;
  }
}

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWiFi();
  syncTime();
  fetchForecast();
}

void loop() {
  int currentHour = getCurrentHour();

  // Re-fetch when hour changes
  if (currentHour != lastFetchedHour && currentHour >= 0) {
    Serial.printf("\n[INFO] Hour changed to %02d:00 — refreshing...\n", currentHour);
    fetchForecast();
    lastFetchedHour = currentHour;
  }

  displayCurrentHourForecast();
  delay(5000); // refresh every 5 seconds
}
