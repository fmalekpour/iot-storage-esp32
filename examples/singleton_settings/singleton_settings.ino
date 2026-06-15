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

// ---- Settings singleton ----
// This manages a single row at "/settings/living-room"
IoTStorageSingleton settings(iotStorage, "/settings/living-room");

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

  // ---- 1. Quick existence check ----
  Serial.println("\n--- Existence Check ---");
  if (settings.exists()) {
    Serial.println("Settings record exists on the server.");
  } else {
    Serial.println("No settings record found on the server yet.");
  }

  // ---- 2. Load settings (with first-boot detection) ----
  Serial.println("\n--- Load Settings ---");

  IoTStorageSingleton::LoadStatus status = settings.load();

  if (status == IoTStorageSingleton::LOAD_OK) {
    // Record exists — read existing values
    Serial.println("LOAD_OK: Existing settings loaded from server.");
    printSettings();

  } else if (status == IoTStorageSingleton::LOAD_NOT_FOUND) {
    // First boot — record does not exist on the server
    Serial.println("LOAD_NOT_FOUND: No settings on server (first boot).");
    Serial.println("Setting defaults and saving...");

    setDefaults();
    if (settings.save()) {
      Serial.println("Defaults saved to server successfully.");
    } else {
      Serial.println("ERROR: Failed to save defaults to server.");
    }

  } else {
    // Connection or server error
    Serial.println("LOAD_ERROR: Cannot reach server — using compile-time fallbacks.");
    // Still apply defaults in memory (won't persist until server is reachable)
    setDefaults();
  }

  // ---- 3. Modify and save ----
  Serial.println("\n--- Modify & Save ---");
  int currentBrightness = settings.getInt("brightness", 75);
  Serial.print("Current brightness: ");
  Serial.println(currentBrightness);

  // Change brightness and persist
  int newBrightness = 30;
  Serial.print("Setting brightness to: ");
  Serial.println(newBrightness);
  settings.setValue("brightness", newBrightness);

  if (settings.save()) {
    Serial.println("New brightness saved successfully.");
  } else {
    Serial.println("ERROR: Failed to save.");
  }

  // ---- 4. Reload to verify persistence ----
  Serial.println("\n--- Reload & Verify ---");
  status = settings.load();
  if (status == IoTStorageSingleton::LOAD_OK) {
    Serial.println("Reloaded from server:");
    printSettings();
  } else {
    Serial.println("Failed to reload settings.");
  }

  Serial.println("\n=== Singleton settings example complete ===");
}

void loop() {
  // Nothing to do — synchronous example
  delay(1000);
}

// ---------------------------------------------------------------------------
// Helper: set factory defaults
// ---------------------------------------------------------------------------

void setDefaults() {
  settings.setValue("brightness", 75);
  settings.setValue("target_temp", 22.0f);
  settings.setValue("mode", "auto");
  settings.setValue("eco", true);
  settings.setValue("auto_off_minutes", 120);
}

// ---------------------------------------------------------------------------
// Helper: print all settings fields
// ---------------------------------------------------------------------------

void printSettings() {
  int   brightness  = settings.getInt("brightness", 70);
  float targetTemp  = settings.getFloat("target_temp", 20.0f);
  const char* mode  = settings.getString("mode", "off");
  bool  eco         = settings.getBool("eco", true);
  int   autoOff     = settings.getInt("auto_off_minutes", 60);

  Serial.println("  Settings:");
  Serial.print("    brightness:       "); Serial.println(brightness);
  Serial.print("    target_temp:      "); Serial.println(targetTemp);
  Serial.print("    mode:             "); Serial.println(mode);
  Serial.print("    eco:              "); Serial.println(eco ? "yes" : "no");
  Serial.print("    auto_off_minutes: "); Serial.println(autoOff);
}
