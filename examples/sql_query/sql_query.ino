#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <IoTStorageClient.h>

// ---- WiFi credentials ----
const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

// ---- IoT Storage server ----
const char* IOT_STORAGE_HOST = "192.168.1.100";
const uint16_t IOT_STORAGE_PORT = 9123;

WiFiClient wifiClient;
IoTStorageClient iotStorage(wifiClient);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  iotStorage.setServer(IOT_STORAGE_HOST, IOT_STORAGE_PORT);
  iotStorage.setTimeout(5000);

  // ---- 1. INSERT multiple rows ----
  Serial.println("\n--- INSERT ---");
  IoTStorageResponse resp = iotStorage.query(
    "INSERT INTO \"/sensors/temp\" (value, unit) VALUES (22.1, 'C'), (22.8, 'C'), (23.5, 'C')"
  );
  if (resp.success()) {
    Serial.print("Inserted "); Serial.print(resp.affected());
    Serial.println(" rows");
  } else {
    Serial.print("Error: "); Serial.println(resp.errorMessage());
  }

  // ---- 2. SELECT all sensors ----
  Serial.println("\n--- SELECT * FROM sensors ---");
  resp = iotStorage.query("SELECT * FROM \"/sensors/temp\"");
  if (resp.success()) {
    Serial.print("Found "); Serial.print(resp.count());
    Serial.println(" records:");
    for (int i = 0; i < resp.count(); i++) {
      JsonObjectConst obj = resp.row(i);
      Serial.print("  ");
      Serial.print(obj["_path"].as<const char*>());
      Serial.print(": value=");
      Serial.print(obj["value"].as<float>());
      Serial.print(", unit=");
      Serial.println(obj["unit"].as<const char*>());
    }
  } else {
    Serial.print("Error: "); Serial.println(resp.errorMessage());
  }

  // ---- 3. SELECT with WHERE + ORDER BY ----
  Serial.println("\n--- SELECT with WHERE + ORDER BY ---");
  resp = iotStorage.query(
    "SELECT * FROM \"/sensors/temp\" WHERE value > 22.5 ORDER BY value DESC"
  );
  if (resp.success()) {
    Serial.print("Found "); Serial.print(resp.count());
    Serial.println(" records (value > 22.5, descending):");
    for (int i = 0; i < resp.count(); i++) {
      JsonObjectConst obj = resp.row(i);
      Serial.print("  value=");
      Serial.println(obj["value"].as<float>());
    }
  }

  // ---- 4. Aggregation ----
  Serial.println("\n--- Aggregation ---");
  resp = iotStorage.query("SELECT AVG(value), MIN(value), MAX(value) FROM \"/sensors/temp\"");
  if (resp.success() && resp.count() > 0) {
    JsonObjectConst agg = resp.row(0);
    Serial.print("  AVG: "); Serial.println(agg["AVG(value)"].as<float>());
    Serial.print("  MIN: "); Serial.println(agg["MIN(value)"].as<float>());
    Serial.print("  MAX: "); Serial.println(agg["MAX(value)"].as<float>());
  }

  // ---- 5. UPDATE ----
  Serial.println("\n--- UPDATE ---");
  resp = iotStorage.query(
    "UPDATE \"/sensors/temp\" SET unit = 'F' WHERE value = 22.1"
  );
  if (resp.success()) {
    Serial.print("Updated "); Serial.print(resp.affected());
    Serial.println(" row(s)");
  }

  // Check the update
  resp = iotStorage.query("SELECT * FROM \"/sensors/temp\" WHERE value = 22.1");
  if (resp.success() && resp.count() > 0) {
    JsonObjectConst obj = resp.row(0);
    Serial.print("  Updated unit: ");
    Serial.println(obj["unit"].as<const char*>());
  }

  // ---- 6. DELETE ----
  Serial.println("\n--- DELETE ---");
  resp = iotStorage.query(
    "DELETE FROM \"/sensors/temp\" WHERE value > 23"
  );
  if (resp.success()) {
    Serial.print("Deleted "); Serial.print(resp.affected());
    Serial.println(" row(s)");
  }

  // Verify remaining records
  resp = iotStorage.query("SELECT COUNT(*) FROM \"/sensors/temp\"");
  if (resp.success() && resp.count() > 0) {
    Serial.print("Remaining records: ");
    Serial.println(resp.row(0)["COUNT(*)"].as<int>());
  }

  // ---- 7. Wildcard SELECT ----
  Serial.println("\n--- Wildcard SELECT ---");
  iotStorage.putValue("/sensors/humidity", "value", 65.0f);
  iotStorage.putValue("/dev/light/kitchen", "state", "on");

  resp = iotStorage.query("SELECT * FROM \"/#\"");
  if (resp.success()) {
    Serial.print("All records in database: ");
    Serial.println(resp.count());
  }

  Serial.println("\n=== SQL query example complete ===");
}

void loop() {
  delay(1000);
}
