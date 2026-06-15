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
const char* IOT_STORAGE_HOST = "192.168.1.100";  // Change to your server IP
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
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Configure IoT Storage client
  iotStorage.setServer(IOT_STORAGE_HOST, IOT_STORAGE_PORT);
  iotStorage.setTimeout(5000);

  // ---- 1. Health check ----
  Serial.println("\n--- Health Check ---");
  IoTStorageHealth h = iotStorage.health();
  if (h.ok) {
    Serial.println("Server is healthy!");
    Serial.print("  Uptime: "); Serial.println(h.uptime);
    Serial.print("  Backend: "); Serial.println(h.backend);
    Serial.print("  Version: "); Serial.println(h.version);
  } else {
    Serial.println("Health check failed — is the server running?");
    return;
  }

  // ---- 2. PUT data ----
  Serial.println("\n--- Put Data ---");
  JsonDocument doc;
  doc["value"] = 23.5;
  doc["unit"] = "C";
  doc["status"] = "active";

  IoTStorageResponse resp = iotStorage.putData("/sensors/temp", doc);
  if (resp.success()) {
    Serial.println("Data inserted successfully!");
    Serial.print("  Path: ");
    Serial.println(resp.json()["_path"].as<const char*>());
    Serial.print("  Value: ");
    Serial.println(resp.json()["value"].as<float>());
  } else {
    Serial.print("Error: ");
    Serial.println(resp.errorMessage());
  }

  // ---- 3. GET data ----
  Serial.println("\n--- Get Data ---");
  resp = iotStorage.getData("/sensors/temp");
  if (resp.success()) {
    Serial.println("Data retrieved successfully!");
    Serial.print("  Path: ");
    Serial.println(resp.json()["_path"].as<const char*>());
    Serial.print("  Value: ");
    Serial.println(resp.json()["value"].as<float>());
    Serial.print("  Unit: ");
    Serial.println(resp.json()["unit"].as<const char*>());
  } else {
    Serial.print("Error: ");
    Serial.println(resp.errorMessage());
  }

  // ---- 4. Convenience methods ----
  Serial.println("\n--- Convenience Methods ---");

  // Insert humidity
  iotStorage.putValue("/sensors/humidity", "value", 65.0f);
  iotStorage.putValue("/sensors/humidity", "unit", "%");

  // Read back using typed methods
  float temp = iotStorage.getFloat("/sensors/temp", "value", -999.0f);
  float humidity = iotStorage.getFloat("/sensors/humidity", "value", -999.0f);
  const char* unit = iotStorage.getString("/sensors/temp", "unit", "?");

  Serial.print("  Temperature: "); Serial.print(temp);
  Serial.print(" "); Serial.println(unit);
  Serial.print("  Humidity: "); Serial.print(humidity);
  Serial.println(" %");

  // ---- 5. DELETE data ----
  Serial.println("\n--- Delete Data ---");
  resp = iotStorage.delData("/sensors/humidity");
  if (resp.success()) {
    Serial.print("Deleted "); Serial.print(resp.affected());
    Serial.println(" record(s)");
  }

  Serial.println("\n=== Basic example complete ===");
}

void loop() {
  // Nothing to do — synchronous example
  delay(1000);
}
