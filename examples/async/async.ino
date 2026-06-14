#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <IoTDBClient.h>

// ---- WiFi credentials ----
const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

// ---- IoTDB server ----
const char* IOTDB_HOST = "192.168.1.100";
const uint16_t IOTDB_PORT = 9123;

WiFiClient wifiClient;
IoTDBClient iotdb(wifiClient);

// Track whether WiFi is connected
bool wifiReady = false;

// ---- Callback for async query ----
void onSensorInsert(IoTDBResponse& resp) {
  Serial.println("\n[ASYNC] Sensor insert callback fired!");
  if (resp.success()) {
    Serial.print("  Inserted "); Serial.print(resp.affected());
    Serial.println(" row(s)");
  } else {
    Serial.print("  Error: ");
    Serial.println(resp.errorMessage());
  }

  // Chain: now fetch the data
  iotdb.getDataAsync("/sensors/temp", onSensorRead);
}

// ---- Callback for async GET ----
void onSensorRead(IoTDBResponse& resp) {
  Serial.println("\n[ASYNC] Sensor read callback fired!");
  if (resp.success()) {
    if (resp.json().containsKey("_path")) {
      Serial.print("  Path: ");
      Serial.println(resp.json()["_path"].as<const char*>());
      Serial.print("  Value: ");
      Serial.println(resp.json()["value"].as<float>());
    }
  } else {
    Serial.print("  Error: ");
    Serial.println(resp.errorMessage());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection timed out!");
    return;
  }

  // Configure IoTDB client
  iotdb.setServer(IOTDB_HOST, IOTDB_PORT);
  iotdb.setTimeout(5000);

  // ---- Fire async requests ----
  Serial.println("\nFiring async requests...");

  // 1. Sync health check (health is not available as async — use sync for this)
  IoTDBHealth h = iotdb.health();
  if (h.ok) {
    Serial.println("[SYNC] Server is healthy!");
  } else {
    Serial.println("[SYNC] Health check failed");
  }

  // 2. Async sensor insert — chains to onSensorRead via callback
  iotdb.queryAsync(
    "INSERT INTO \"/sensors/temp\" (value, unit) VALUES (24.0, 'C')",
    onSensorInsert
  );

  Serial.println("Async requests queued. Polling via loop()...");
}

void loop() {
  // Drive the async state machine
  iotdb.loop();

  // Your main code can do other things here —
  // the IoTDB client will process async requests in the background
  // without blocking your main loop.

  delay(10);  // Small delay to avoid busy-waiting
}
