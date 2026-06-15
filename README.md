# IoTStorageClient — PlatformIO Library for IoT Storage

**C++ client library for [IoT Storage](https://github.com/iot-storage/iot-storage) — a path-based lightweight SQL database with REST API.**

Think MQTT topics meets SQL. Every path is a record. Query with SQL. Built for IoT and edge deployments, now accessible from your ESP32, ESP8266, and Arduino projects.

---

## Features

- 🔌 **Full REST API coverage** — `GET /data/:path`, `PUT /data/:path`, `DELETE /data/:path`, `POST /query`, `GET /health`
- 📡 **SQL support** — INSERT, SELECT, UPDATE, DELETE with WHERE, ORDER BY, LIMIT, GROUP BY, aggregations
- 🌿 **MQTT-style wildcard paths** — `+` (single-level) and `#` (multi-level)
- ⚡ **Sync & Async modes** — blocking for simple scripts, callback-based non-blocking for responsive systems
- 🎯 **Typed convenience methods** — `getInt()`, `getFloat()`, `getString()`, `getBool()`, `putValue()`
- �️ **Settings Singleton** — `IoTStorageSingleton` — load/save single-row settings with built-in "first boot" detection
- �📦 **Single dependency** — ArduinoJson ^7.0
- 🌍 **Cross-platform** — ESP32, ESP8266, Arduino MKR, Nano RP2040, and any board with a `Client` implementation
- 🔐 **SSL-ready** — Pass `WiFiClientSecure` for HTTPS

---

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    iot-storage/IoTStorageClient @ ^1.0.1
```

Or install directly from GitHub:

```ini
lib_deps =
    https://github.com/fmalekpour/iot-storage-esp32.git
```

Then `#include <IoTStorageClient.h>` in your sketch.

### Manual (Arduino IDE / other)

1. Download this repository as ZIP
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library…**
3. Install **ArduinoJson** via Library Manager (Sketch → Include Library → Manage Libraries → search "ArduinoJson")

---

## Quick Start

```cpp
#include <WiFi.h>
#include <IoTStorageClient.h>

WiFiClient wifi;
IoTStorageClient db(wifi);

void setup() {
  Serial.begin(115200);
  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);

  db.setServer("192.168.1.100", 9123);

  // Insert a sensor reading
  JsonDocument doc;
  doc["value"] = 23.5;
  doc["unit"] = "C";
  db.putData("/sensors/temp", doc);

  // Read it back
  float temp = db.getFloat("/sensors/temp", "value");
  Serial.printf("Temperature: %.1f\n", temp);
}

void loop() { }
```

---

## API Reference

### Constructor & Configuration

#### `IoTStorageClient(Client& client)`

Create a client bound to any Arduino `Client` instance. You manage the client's lifetime.

```cpp
WiFiClient wifiClient;
IoTStorageClient iotStorage(wifiClient);

// For HTTPS:
// WiFiClientSecure sslClient;
// IoTStorageClient iotStorage(sslClient);
```

#### `void setServer(const char* host, uint16_t port = 9123)`

Set the IoT Storage server address. Must be called before any API calls.

#### `void setTimeout(uint32_t ms)`

Set HTTP request timeout in milliseconds. Default: `5000`.

---

### Synchronous API (Blocking)

All synchronous methods return immediately after the HTTP round-trip completes.

#### `IoTStorageResponse query(const char* sql)`

Execute a raw SQL statement via `POST /query`.

```cpp
IoTStorageResponse resp = iotStorage.query(
  "SELECT * FROM \"/sensors/+\" WHERE value > 20 ORDER BY value DESC LIMIT 10"
);
if (resp.success()) {
  for (int i = 0; i < resp.count(); i++) {
    JsonObjectConst row = resp.row(i);
    Serial.println(row["_path"].as<const char*>());
  }
} else {
  Serial.println(resp.errorMessage());
}
```

#### `IoTStorageResponse getData(const char* path)`

Fetch record(s) via `GET /data/:path`. Supports wildcards `+` and `#`.

```cpp
// Exact path — returns single record
IoTStorageResponse resp = iotStorage.getData("/sensors/temp");

// Single wildcard — returns all direct children
resp = iotStorage.getData("/sensors/+");

// Multi-level wildcard — returns all descendants
resp = iotStorage.getData("/sensors/#");
```

#### `IoTStorageResponse putData(const char* path, const JsonDocument& data)`

Upsert a record via `PUT /data/:path`. Wildcards are NOT allowed.

```cpp
JsonDocument doc;
doc["value"] = 23.5;
doc["unit"] = "C";
doc["status"] = "active";
IoTStorageResponse resp = iotStorage.putData("/sensors/temp", doc);
```

#### `IoTStorageResponse delData(const char* path)`

Delete record(s) via `DELETE /data/:path`. Supports wildcards.

```cpp
IoTStorageResponse resp = iotStorage.delData("/sensors/temp");
Serial.printf("Deleted %d records\n", resp.affected());
```

#### `IoTStorageHealth health()`

Check server health via `GET /health`.

```cpp
IoTStorageHealth h = iotStorage.health();
if (h.ok) {
  Serial.printf("Server up for %ld seconds, version %s\n", h.uptime, h.version);
}
```

#### `IoTStorageServerInfo info()`

Get server metadata via `GET /`.

```cpp
IoTStorageServerInfo inf = iotStorage.info();
Serial.printf("Connected to %s v%s\n", inf.name, inf.version);
```

---

### IoTStorageResponse

Every synchronous method returns an `IoTStorageResponse`:

| Method | Returns | Description |
|--------|---------|-------------|
| `success()` | `bool` | `true` if HTTP 2xx and no server error |
| `errorMessage()` | `const char*` | Error string, empty if success |
| `httpStatus()` | `int` | Raw HTTP status code |
| `responseType()` | `const char*` | `"select"`, `"insert"`, `"update"`, or `"delete"` |
| `count()` | `int` | Number of rows (SELECT queries) |
| `affected()` | `int` | Rows affected (INSERT/UPDATE/DELETE) |
| `rows()` | `JsonArrayConst` | Rows array for iteration |
| `row(int)` | `JsonObjectConst` | Single row by index (0-based) |
| `json()` | `JsonDocument&` | Full JSON for power users |

---

### Async API (Non-Blocking)

Fire requests without blocking. Call `loop()` in your main `loop()` to process them. Callbacks fire when responses arrive.

```cpp
void onData(IoTStorageResponse& resp) {
  if (resp.success()) {
    Serial.println("Got data!");
  }
}

void setup() {
  // ... WiFi + iotStorage configuration ...

  // Queue async requests
  iotStorage.queryAsync("SELECT * FROM \"/sensors/+\"", onData);
  iotStorage.getDataAsync("/sensors/temp", onData);
}

void loop() {
  iotStorage.loop();   // <-- Drive the async state machine
  delay(10);           // Avoid busy-waiting
}
```

| Async Method | Equivalent Sync |
|-------------|-----------------|
| `queryAsync(sql, callback)` | `query(sql)` |
| `getDataAsync(path, callback)` | `getData(path)` |
| `putDataAsync(path, data, callback)` | `putData(path, data)` |
| `delDataAsync(path, callback)` | `delData(path)` |

**Async limits:**
- Up to 4 pending requests queued
- Requests processed one at a time (FIFO)
- Queue overflow triggers immediate error callback

---

### Convenience Methods

Typed access without JSON boilerplate. Ideal for simple single-field sensor records.

```cpp
// Write
iotStorage.putValue("/sensors/temp",     "value", 23.5f);         // float
iotStorage.putValue("/sensors/temp",     "unit",  "C");           // string
iotStorage.putValue("/dev/light/kitchen", "state", 1);            // int
iotStorage.putValue("/dev/light/kitchen", "on",    true);         // bool

// Read
float       temp  = iotStorage.getFloat("/sensors/temp",    "value", -999.0f);
const char* unit  = iotStorage.getString("/sensors/temp",   "unit",  "?");
int         state = iotStorage.getInt("/dev/light/kitchen",  "state", -1);
bool        on    = iotStorage.getBool("/dev/light/kitchen", "on",    false);
```

The third parameter is the fallback value if the record/field is missing.

---

### IoTStorageSingleton — Production Settings Wrapper

`IoTStorageSingleton` manages a **single JSON row** at a path (e.g. `/settings/mydevice`).
It keeps an in-memory document — reads and writes are local, `save()` persists to the server.
`load()` distinguishes three states: record exists, record not found (first boot), and connection error.

#### `IoTStorageSingleton(IoTStorageClient& client, const char* path)`

Bind to an existing `IoTStorageClient` and a data path.

```cpp
WiFiClient wifi;
IoTStorageClient db(wifi);
db.setServer("192.168.1.100", 9123);

IoTStorageSingleton settings(db, "/settings/living-room");
```

#### `LoadStatus load()`

Fetch the record from the server. Returns one of three values:

| Return | Meaning | What to do |
|--------|---------|------------|
| `LOAD_OK` | Record exists on server — fields populated | Call `getInt()` / `getFloat()` / … |
| `LOAD_NOT_FOUND` | No record on server (404) — first boot | Set defaults with `setValue()`, then `save()` |
| `LOAD_ERROR` | Connection or server error | Use compile-time fallbacks, retry later |

```cpp
IoTStorageSingleton::LoadStatus s = settings.load();

if (s == IoTStorageSingleton::LOAD_OK) {
  // Record exists — read values
  int brightness = settings.getInt("brightness", 75);
  float targetTemp = settings.getFloat("target_temp", 22.0);

} else if (s == IoTStorageSingleton::LOAD_NOT_FOUND) {
  // First boot — set defaults and save
  settings.setValue("brightness", 75);
  settings.setValue("target_temp", 22.0f);
  settings.setValue("mode", "auto");
  settings.save();

} else {
  // Network or server error
  Serial.println("Failed to load settings (connection error).");
}
```

#### `bool save()`

Persist the in-memory document to the server via `PUT /data/:path`. Returns `true` on success.

```cpp
settings.setValue("brightness", 50);
settings.save();
```

#### Typed Getters (in-memory, no HTTP)

| Method | Returns | Default |
|--------|---------|---------|
| `getInt(field, default)` | `int` | `0` |
| `getFloat(field, default)` | `float` | `0.0f` |
| `getString(field, default)` | `const char*` | `""` |
| `getBool(field, default)` | `bool` | `false` |

#### Typed Setters (in-memory, no HTTP)

| Method | Type |
|--------|------|
| `setValue(field, int)` | `int` |
| `setValue(field, float)` | `float` |
| `setValue(field, const char*)` | `const char*` |
| `setValue(field, bool)` | `bool` |

#### `bool exists()`

Quick server-side check — performs `GET /data/:path`. Returns `true` if the record exists (200), `false` if 404 or connection error.

```cpp
if (settings.exists()) {
  Serial.println("Settings record exists on server.");
}
```

#### `JsonDocument& data()` / `const JsonDocument& data() const`

Direct access to the underlying JSON document for advanced use cases.

#### `const char* path() const`

Returns the configured data path.

---

## SQL Quick Reference

The `query()` method accepts standard SQL passed through to IoT Storage:

```cpp
// INSERT
iotStorage.query("INSERT INTO \"/sensors/temp\" (value, unit) VALUES (23.5, 'C')");
iotStorage.query("INSERT INTO \"/sensors/temp\" (value, unit) VALUES (24.1, 'C'), (22.8, 'C')");

// SELECT
iotStorage.query("SELECT * FROM \"/sensors/temp\"");
iotStorage.query("SELECT * FROM \"/sensors/+\" WHERE value > 20");
iotStorage.query("SELECT * FROM \"/sensors/#\" ORDER BY value DESC LIMIT 10");

// UPDATE
iotStorage.query("UPDATE \"/sensors/temp\" SET unit = 'F' WHERE value = 22.1");

// DELETE
iotStorage.query("DELETE FROM \"/sensors/+\" WHERE value IS NULL");

// Aggregation
iotStorage.query("SELECT AVG(value), COUNT(*) FROM \"/sensors/+\"");
iotStorage.query("SELECT unit, AVG(value) FROM \"/sensors/+\" GROUP BY unit");
```

For the full SQL reference, see the [IoT Storage README](https://github.com/iot-storage/iot-storage).

---

## Compatible Boards

Any board that runs Arduino framework and has a `Client` implementation:

| Board Family | WiFi | Ethernet | PlatformIO Platform |
|-------------|------|----------|-------------------|
| **ESP32** | ✅ | — | `espressif32` |
| **ESP8266** | ✅ | — | `espressif8266` |
| **Arduino MKR WiFi 1010** | ✅ | — | `atmelsam` |
| **Arduino Nano RP2040 Connect** | ✅ | — | `raspberrypi` |
| **Arduino Uno R4 WiFi** | ✅ | — | `renesas-ra` |
| **Arduino MKR ETH Shield** | — | ✅ | `atmelsam` |
| **Teensy 4.1 (Ethernet)** | — | ✅ | `teensy` |

---

## Dependencies

| Library | Version | Required |
|---------|---------|----------|
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^7.0.0 | ✅ Yes |

PlatformIO installs this automatically when you add IoTStorageClient as a dependency.

---

## Examples

See the `examples/` directory:

| Example | Description |
|---------|-------------|
| [`basic`](examples/basic/basic.ino) | WiFi connect, health check, PUT/GET/DELETE, convenience methods |
| [`sql_query`](examples/sql_query/sql_query.ino) | INSERT, SELECT with WHERE/ORDER BY, aggregation, UPDATE, DELETE |
| [`async`](examples/async/async.ino) | Async callback-based requests, chaining, non-blocking loop |
| [`singleton_settings`](examples/singleton_settings/singleton_settings.ino) | IoTStorageSingleton — first-boot detection, load/save defaults |

---

## License

MIT © IoT Storage Contributors. See [LICENSE](LICENSE).

---

## Links

- [IoT Storage Server](https://github.com/iot-storage/iot-storage) — The database this client connects to
- [PlatformIO Registry](https://registry.platformio.org/) — Find this library
- [ArduinoJson](https://arduinojson.org/) — JSON library used internally
