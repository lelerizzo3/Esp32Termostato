#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"

#ifndef USE_MOCK
#define USE_MOCK 1
#endif

#if !USE_MOCK
#include <Wire.h>
#include <Adafruit_SHT4x.h>
Adafruit_SHT4x sht4;
#endif

WebServer server(80);

// Legge temperatura e umidità: dati simulati (mock) o reali (SHT40)
bool readSensor(float &t, float &h) {
#if USE_MOCK
  // Onda lenta (periodo ~60 s) + piccolo rumore casuale
  float phase = (millis() / 60000.0f) * 2.0f * PI;
  t = 22.0f + 3.0f * sinf(phase) + random(-10, 11) / 100.0f;
  h = 50.0f + 10.0f * cosf(phase) + random(-10, 11) / 10.0f;
  return true;
#else
  sensors_event_t humidity, temp;
  if (!sht4.getEvent(&humidity, &temp)) return false;
  t = temp.temperature;
  h = humidity.relative_humidity;
  return true;
#endif
}

void handleRoot() {
  float t, h;
  if (!readSensor(t, h)) {
    server.send(500, "application/json", "{\"error\":\"lettura sensore fallita\"}");
    return;
  }
  String json = "{\"temperature\":" + String(t, 2) +
                ",\"humidity\":" + String(h, 1) +
                ",\"mock\":" + String(USE_MOCK ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

#if USE_MOCK
  Serial.println("Modalità MOCK: dati simulati");
#else
  Wire.begin(21, 22);  // SDA, SCL
  if (!sht4.begin()) {
    Serial.println("SHT40 non trovato: controlla i collegamenti");
    while (true) delay(1000);
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("Modalità REALE: SHT40 pronto");
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connessione Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
}