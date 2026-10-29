#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"
#include <Wire.h>
#include <Adafruit_SHT4x.h>
Adafruit_SHT4x sht4;

#define I2C_SDA 17
#define I2C_SCL 16

WebServer server(80);

static const unsigned long SENSOR_READ_INTERVAL_MS = 5000;

struct SensorState {
  float temperature = NAN;
  float humidity = NAN;
  bool valid = false;
  unsigned long lastAttemptMs = 0;
  unsigned long lastSuccessMs = 0;
} sensorState;

// Legge temperatura e umidità: dati simulati (mock) o reali (SHT40)
bool readSensor(float &t, float &h) {
  sensors_event_t humidity, temp;
  if (!sht4.getEvent(&humidity, &temp)) return false;
  t = temp.temperature;
  h = humidity.relative_humidity;
  return true;
}

// CRC-8 Sensirion (poly 0x31, init 0xFF), usato dal protocollo SHT4x
static uint8_t shtCrc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
  }
  return crc;
}

// Lettura grezza di diagnostica: ticks ADC + esito CRC
bool readSensorRaw(uint16_t &tTicks, uint16_t &rhTicks, bool &crcOk) {
  Wire.beginTransmission(0x44);
  Wire.write(0xFD);  // high precision, no heater, no clock stretching
  if (Wire.endTransmission() != 0) return false;
  delay(10);
  if (Wire.requestFrom(0x44, 6) != 6) return false;

  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  crcOk = (b[2] == shtCrc8(b, 2)) && (b[5] == shtCrc8(b + 3, 2));
  tTicks = ((uint16_t)b[0] << 8) | b[1];
  rhTicks = ((uint16_t)b[3] << 8) | b[4];
  return true;
}

// Aggiorna lo stato del sensore al massimo ogni SENSOR_READ_INTERVAL_MS
// senza bloccare le richieste HTTP in attesa della lettura.
void updateSensor() {
  unsigned long now = millis();
  if (now - sensorState.lastAttemptMs < SENSOR_READ_INTERVAL_MS) return;
  sensorState.lastAttemptMs = now;

  float t, h;
  if (readSensor(t, h)) {
    sensorState.temperature = t;
    sensorState.humidity = h;
    sensorState.lastSuccessMs = now;
    sensorState.valid = true;
    Serial.printf("SHT40: T=%.2f C  RH=%.1f %%\n", t, h);
  } else {
    Serial.println("Lettura sensore fallita: mantengo l'ultimo valore valido");
  }
}

void handleRoot() {
  if (!sensorState.valid) {
    server.send(503, "application/json", "{\"error\":\"lettura sensore non ancora disponibile\"}");
    return;
  }
  unsigned long age = millis() - sensorState.lastSuccessMs;
  String json = "{\"temperature\":" + String(sensorState.temperature, 2) +
                ",\"humidity\":" + String(sensorState.humidity, 1) +
                ",\"uom\": \"Celsius\"" +
                ",\"age_ms\":" + String(age) + "}";
  server.send(200, "application/json", json);
}

// Endpoint di diagnostica: mostra i valori grezzi restituiti dal chip,
// l'esito del CRC e i valori calcolati con la formula ufficiale Sensirion.
void handleDebug() {
  uint16_t tTicks, rhTicks;
  bool crcOk;
  if (!readSensorRaw(tTicks, rhTicks, crcOk)) {
    server.send(500, "application/json", "{\"error\":\"lettura I2C fallita\"}");
    return;
  }

  float t = -45.0f + 175.0f * tTicks / 65535.0f;
  float h = -6.0f + 125.0f * rhTicks / 65535.0f;
  h = constrain(h, 0.0f, 100.0f);

  String json = "{\"raw_t\":" + String(tTicks) +
                ",\"raw_rh\":" + String(rhTicks) +
                ",\"crc_ok\":" + String(crcOk ? "true" : "false") +
                ",\"temperature\":" + String(t, 2) +
                ",\"humidity\":" + String(h, 1) +
                ",\"serial\":\"" + String(sht4.readSerial(), HEX) + "\"}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!sht4.begin()) {
    Serial.println("SHT40 non trovato: controlla i collegamenti");
    while (true) delay(1000);
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("Modalità REALE: SHT40 pronto");


  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connessione Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  // Forza una prima lettura al primo giro del loop
  sensorState.lastAttemptMs = millis() - SENSOR_READ_INTERVAL_MS;

  server.on("/", handleRoot);
  server.on("/debug", handleDebug);
  server.begin();
}

void loop() {
  updateSensor();
  server.handleClient();
}