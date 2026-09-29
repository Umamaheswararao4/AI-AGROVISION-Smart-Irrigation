/*
  Smart Irrigation System - ESP32 (WiFi version)
  Sensors: DHT11/DHT22 (temp + humidity), Soil Moisture (analog),
           Rain Sensor (analog)
  Output : Water pump via relay module

  Libraries needed (Library Manager):
    - "DHT sensor library" by Adafruit
    - "Adafruit Unified Sensor" by Adafruit
  (WiFi.h and WebServer.h come with the ESP32 board package)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ---------------- WIFI CONFIG ----------------
const char* WIFI_SSID     = "Kumar@wifi";
const char* WIFI_PASSWORD = "kiran1234";

WebServer server(80);

// ---------------- PIN CONFIG ----------------
#define DHT_PIN     4       // DHT data pin
#define DHT_TYPE    DHT11   // change to DHT22 if you use DHT22
#define SOIL_PIN    34      // Soil moisture AO (ADC1)
#define RAIN_PIN    35      // Rain sensor AO   (ADC1)
#define PUMP_PIN    26      // Relay IN pin

// Most relay modules are ACTIVE LOW (LOW = ON). Change if yours is opposite.
#define RELAY_ON    LOW
#define RELAY_OFF   HIGH

// ---------------- CALIBRATION ----------------
const int SOIL_DRY_VALUE = 0;  // raw reading in dry air  (HIGH number)
const int SOIL_WET_VALUE = 1500;  // raw reading in water    (LOW number)

const int RAIN_DRY_VALUE = 4095;  // raw reading when no rain
const int RAIN_WET_VALUE = 1000;  // raw reading when fully wet
const int RAIN_THRESHOLD = 2500;  // below this = rain detected

// ---------------- PUMP LOGIC ----------------
const int SOIL_PUMP_ON_BELOW  = 35;          // % - start pump below this
const int SOIL_PUMP_OFF_ABOVE = 60;          // % - stop pump above this
const unsigned long PUMP_MAX_RUN_MS = 60000; // safety: max 60 s per run

DHT dht(DHT_PIN, DHT_TYPE);

// ---------------- GLOBAL VALUES ----------------
float temperature = 0;
float humidity    = 0;
int   soilRaw = 0, soilMoisture = 0;
int   rainRaw = 0, rainPercent  = 0;
bool  isRaining = false;
bool  pumpOn = false;
unsigned long pumpStartTime = 0;
unsigned long lastRead = 0;

// ---------------- HELPERS ----------------
int readAverage(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return sum / samples;
}

void setPump(bool state) {
  pumpOn = state;
  digitalWrite(PUMP_PIN, state ? RELAY_ON : RELAY_OFF);
  if (state) pumpStartTime = millis();
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed (system will keep running offline).");
  }
}

// Allow the dashboard (any origin) to read data from the ESP32
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-store");
}

// ---------------- WEB PAGES ----------------
void handleData() {
  addCORS();
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"soil_percent\":" + String(soilMoisture) + ",";
  json += "\"soil_raw\":" + String(soilRaw) + ",";
  json += "\"rain\":\"" + String(isRaining ? "Rain Detected" : "No Rain") + "\",";
  json += "\"rain_percent\":" + String(rainPercent) + ",";
  json += "\"rain_raw\":" + String(rainRaw) + ",";
  json += "\"pump\":\"" + String(pumpOn ? "ON" : "OFF") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleOptions() {
  addCORS();
  server.send(204);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>Smart Irrigation</title>";
  html += "<style>body{font-family:Arial;background:#f1f8e9;text-align:center;}";
  html += ".card{background:#fff;margin:10px auto;padding:12px;max-width:340px;";
  html += "border-radius:10px;box-shadow:0 2px 6px #aaa;}";
  html += "h1{color:#2e7d32;}</style></head><body>";
  html += "<h1>Smart Irrigation</h1>";
  html += "<div class='card'>Temperature: <b>" + String(temperature, 1) + " &deg;C</b></div>";
  html += "<div class='card'>Humidity: <b>" + String(humidity, 1) + " %</b></div>";
  html += "<div class='card'>Soil Moisture: <b>" + String(soilMoisture) + " %</b> (raw " + String(soilRaw) + ")</div>";
  html += "<div class='card'>Rain: <b>" + String(isRaining ? "Rain Detected" : "No Rain") + "</b> (raw " + String(rainRaw) + ")</div>";
  html += "<div class='card'>Water Pump: <b>" + String(pumpOn ? "ON" : "OFF") + "</b></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, RELAY_OFF);   // pump OFF at start

  analogReadResolution(12);            // 0 - 4095
  dht.begin();

  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/data", HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("Smart Irrigation System Started");
}

// ---------------- LOOP ----------------
void loop() {
  server.handleClient();

  // Reconnect WiFi if it drops
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      Serial.println("WiFi lost, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // Read sensors every 2 seconds (non-blocking)
  if (millis() - lastRead < 2000) return;
  lastRead = millis();

  // ---- Read sensors ----
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT sensor read failed! Check wiring.");
  } else {
    temperature = t;
    humidity = h;
  }

  soilRaw = readAverage(SOIL_PIN);
  rainRaw = readAverage(RAIN_PIN);

  // ---- Convert values ----
  soilMoisture = map(soilRaw, SOIL_DRY_VALUE, SOIL_WET_VALUE, 0, 100);
  soilMoisture = constrain(soilMoisture, 0, 100);

  rainPercent = map(rainRaw, RAIN_DRY_VALUE, RAIN_WET_VALUE, 0, 100);
  rainPercent = constrain(rainPercent, 0, 100);

  isRaining = (rainRaw < RAIN_THRESHOLD);

  // ---- Pump control ----
  if (!pumpOn) {
    if (soilMoisture < SOIL_PUMP_ON_BELOW && !isRaining) {
      setPump(true);
    }
  } else {
    bool soilWetEnough = soilMoisture > SOIL_PUMP_OFF_ABOVE;
    bool timeout = (millis() - pumpStartTime) > PUMP_MAX_RUN_MS;
    if (soilWetEnough || isRaining || timeout) {
      setPump(false);
    }
  }

  // ---- Print to Serial Monitor ----
  Serial.print("Temperature: ");   Serial.print(temperature);  Serial.println(" °C");
  Serial.print("Humidity: ");      Serial.print(humidity);     Serial.println(" %");
  Serial.print("Soil Moisture: "); Serial.print(soilMoisture);
  Serial.print(" %  (raw: ");      Serial.print(soilRaw);      Serial.println(")");
  Serial.print("Rain Status: ");   Serial.print(isRaining ? "Rain Detected" : "No Rain");
  Serial.print("  (raw: ");        Serial.print(rainRaw);      Serial.println(")");
  Serial.print("Water Pump: ");    Serial.println(pumpOn ? "ON" : "OFF");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Dashboard: http://"); Serial.println(WiFi.localIP());
  }
  Serial.println("--");
}
