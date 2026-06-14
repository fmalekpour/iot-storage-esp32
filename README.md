# IoTDBClient — PlatformIO Library for IoTDB

**C++ client library for [IoTDB](https://github.com/iotdb/iotdb) — a path-based lightweight SQL database with REST API.**

Think MQTT topics meets SQL. Every path is a record. Query with SQL. Built for IoT and edge deployments, now accessible from your ESP32, ESP8266, and Arduino projects.

---

## Features

- 🔌 **Full REST API coverage** — `GET /data/:path`, `PUT /data/:path`, `DELETE /data/:path`, `POST /query`, `GET /health`
- 📡 **SQL support** — INSERT, SELECT, UPDATE, DELETE with WHERE, ORDER BY, LIMIT, GROUP BY, aggregations
- 🌿 **MQTT-style wildcard paths** — `+` (single-level) and `#` (multi-level)
- ⚡ **Sync & Async modes** — blocking for simple scripts, callback-based non-blocking for responsive systems
- 🎯 **Typed convenience methods** — `getInt()`, `getFloat()`, `getString()`, `putValue()`
- 📦 **Single dependency** — ArduinoJson ^7.0
- 🌍 **Cross-platform** — ESP32, ESP8266, Arduino MKR, Nano RP2040, and any board with a `Client` implementation
- 🔐 **SSL-ready** — Pass `WiFiClientSecure` for HTTPS

---

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    iotdb/IoTDBClient @ ^1.0.0
```

Then `#include <IoTDBClient.h>` in your sketch.

### Manual (Arduino IDE / other)

1. Download this repository as ZIP
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library…**
3. Install **ArduinoJson** via Library Manager (Sketch → Include Library → Manage Libraries → search "ArduinoJson")

---

## Quick Start

```cpp
#include <WiFi.h>
#include <IoTDBClient.h>

WiFiClient wifi;
IoTDBClient db(wifi);

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

#### `IoTDBClient(Client& client)`

Create a client bound to any Arduino `Client` instance. You manage the client's lifetime.

```cpp
WiFiClient wifiClient;
IoTDBClient iotdb(wifiClient);

// For HTTPS:
// WiFiClientSecure sslClient;
// IoTDBClient iotdb(sslClient);
```

#### `void setServer(const char* host, uint16_t port = 9123)`

Set the IoTDB server address. Must be called before any API calls.

#### `void setTimeout(uint32_t ms)`

Set HTTP request timeout in milliseconds. Default: `5000`.

---

### Synchronous API (Blocking)

All synchronous methods return immediately after the HTTP round-trip completes.

#### `IoTDBResponse query(const char* sql)`

Execute a raw SQL statement via `POST /query`.

```cpp
IoTDBResponse resp = iotdb.query(
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

#### `IoTDBResponse getData(const char* path)`

Fetch record(s) via `GET /data/:path`. Supports wildcards `+` and `#`.

```cpp
// Exact path — returns single record
IoTDBResponse resp = iotdb.getData("/sensors/temp");

// Single wildcard — returns all direct children
resp = iotdb.getData("/sensors/+");

// Multi-level wildcard — returns all descendants
resp = iotdb.getData("/sensors/#");
```

#### `IoTDBResponse putData(const char* path, const JsonDocument& data)`

Upsert a record via `PUT /data/:path`. Wildcards are NOT allowed.

```cpp
JsonDocument doc;
doc["value"] = 23.5;
doc["unit"] = "C";
doc["status"] = "active";
IoTDBResponse resp = iotdb.putData("/sensors/temp", doc);
```

#### `IoTDBResponse delData(const char* path)`

Delete record(s) via `DELETE /data/:path`. Supports wildcards.

```cpp
IoTDBResponse resp = iotdb.delData("/sensors/temp");
Serial.printf("Deleted %d records\n", resp.affected());
```

#### `IoTDBHealth health()`

Check server health via `GET /health`.

```cpp
IoTDBHealth h = iotdb.health();
if (h.ok) {
  Serial.printf("Server up for %ld seconds, version %s\n", h.uptime, h.version);
}
```

#### `IoTDBServerInfo info()`

Get server metadata via `GET /`.

```cpp
IoTDBServerInfo inf = iotdb.info();
Serial.printf("Connected to %s v%s\n", inf.name, inf.version);
```

---

### IoTDBResponse

Every synchronous method returns an `IoTDBResponse`:

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
void onData(IoTDBResponse& resp) {
  if (resp.success()) {
    Serial.println("Got data!");
  }
}

void setup() {
  // ... WiFi + iotdb configuration ...

  // Queue async requests
  iotdb.queryAsync("SELECT * FROM \"/sensors/+\"", onData);
  iotdb.getDataAsync("/sensors/temp", onData);
}

void loop() {
  iotdb.loop();   // <-- Drive the async state machine
  delay(10);      // Avoid busy-waiting
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
iotdb.putValue("/sensors/temp",    "value", 23.5f);        // float
iotdb.putValue("/sensors/temp",    "unit",  "C");          // string
iotdb.putValue("/dev/light/kitchen", "state", 1);          // int
iotdb.putValue("/dev/light/kitchen", "on", true);          // bool

// Read
float       temp  = iotdb.getFloat("/sensors/temp",   "value", -999.0f);
const char* unit  = iotdb.getString("/sensors/temp",  "unit",  "?");
int         state = iotdb.getInt("/dev/light/kitchen", "state", -1);
bool        on    = iotdb.getBool("/dev/light/kitchen", "on", false);
```

The third parameter is the fallback value if the record/field is missing.

---

## SQL Quick Reference

The `query()` method accepts standard SQL passed through to IoTDB:

```cpp
// INSERT
iotdb.query("INSERT INTO \"/sensors/temp\" (value, unit) VALUES (23.5, 'C')");
iotdb.query("INSERT INTO \"/sensors/temp\" (value, unit) VALUES (24.1, 'C'), (22.8, 'C')");

// SELECT
iotdb.query("SELECT * FROM \"/sensors/temp\"");
iotdb.query("SELECT * FROM \"/sensors/+\" WHERE value > 20");
iotdb.query("SELECT * FROM \"/sensors/#\" ORDER BY value DESC LIMIT 10");

// UPDATE
iotdb.query("UPDATE \"/sensors/temp\" SET unit = 'F' WHERE value = 22.1");

// DELETE
iotdb.query("DELETE FROM \"/sensors/+\" WHERE value IS NULL");

// Aggregation
iotdb.query("SELECT AVG(value), COUNT(*) FROM \"/sensors/+\"");
iotdb.query("SELECT unit, AVG(value) FROM \"/sensors/+\" GROUP BY unit");
```

For the full SQL reference, see the [IoTDB README](https://github.com/iotdb/iotdb).

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

PlatformIO installs this automatically when you add IoTDBClient as a dependency.

---

## Examples

See the `examples/` directory:

| Example | Description |
|---------|-------------|
| [`basic`](examples/basic/basic.ino) | WiFi connect, health check, PUT/GET/DELETE, convenience methods |
| [`sql_query`](examples/sql_query/sql_query.ino) | INSERT, SELECT with WHERE/ORDER BY, aggregation, UPDATE, DELETE |
| [`async`](examples/async/async.ino) | Async callback-based requests, chaining, non-blocking loop |

---

## License

MIT © IoTDB Contributors. See [LICENSE](LICENSE).

---

## Links

- [IoTDB Server](https://github.com/iotdb/iotdb) — The database this client connects to
- [PlatformIO Registry](https://registry.platformio.org/) — Find this library
- [ArduinoJson](https://arduinojson.org/) — JSON library used internally
