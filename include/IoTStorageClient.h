#ifndef IOTSTORAGE_CLIENT_H
#define IOTSTORAGE_CLIENT_H

#include <Arduino.h>
#include <Client.h>
#include <ArduinoJson.h>
#include <functional>

// ============================================================================
// IoTStorageResponse — Wraps every API response from the IoT Storage server
// ============================================================================

class IoTStorageResponse {
public:
  IoTStorageResponse();

  /// Whether the HTTP request succeeded and the server returned no error.
  bool success() const;

  /// Human-readable error message, or empty string if successful.
  const char* errorMessage() const;

  /// Raw HTTP status code (200, 400, 404, etc.).
  int httpStatus() const;

  /// Operation type: "select", "insert", "update", "delete", or "".
  const char* responseType() const;

  /// Number of rows returned (SELECT queries).
  int count() const;

  /// Number of rows affected (INSERT / UPDATE / DELETE).
  int affected() const;

  /// Raw JSON document for power users.
  JsonDocument& json();
  const JsonDocument& json() const;

  /// Rows array (convenience for iterating SELECT results).
  JsonArrayConst rows() const;

  /// Single row at index (0-based).
  JsonObjectConst row(int index) const;

  // ---- Internal (used by IoTStorageClient) ----
  void _setError(const char* msg);
  void _setHttpStatus(int code);
  void _parse(const String& responseBody);

private:
  bool         _success;
  String       _errorMessage;
  int          _httpStatus;
  String       _responseType;
  int          _rowCount;
  int          _affectedRows;
  JsonDocument _json;
};

// ============================================================================
// IoTStorageHealth — Server health information
// ============================================================================

struct IoTStorageHealth {
  bool        ok;
  long        uptime;
  const char* backend;
  const char* version;
};

// ============================================================================
// IoTStorageServerInfo — Server metadata
// ============================================================================

struct IoTStorageServerInfo {
  const char* name;
  const char* version;
};

// ============================================================================
// IoTStorageClient — Main client for the IoT Storage REST API
// ============================================================================

class IoTStorageClient {
public:
  // ------------------------------------------------------------------
  // Construction & Configuration
  // ------------------------------------------------------------------

  /// Construct with any Arduino Client (WiFiClient, WiFiClientSecure,
  /// EthernetClient, etc.). The caller manages the client's lifetime.
  explicit IoTStorageClient(Client& client);

  /// Set the IoT Storage server host and port (default port = 9123).
  void setServer(const char* host, uint16_t port = 9123);

  /// Set HTTP request timeout in milliseconds (default = 5000).
  void setTimeout(uint32_t ms);

  /// Get the current timeout value.
  uint32_t timeout() const;

  // ------------------------------------------------------------------
  // Synchronous API — blocking, returns immediately
  // ------------------------------------------------------------------

  /// Execute a raw SQL statement via POST /query.
  /// Returns the parsed response with rows / affected / error info.
  IoTStorageResponse query(const char* sql);

  /// Fetch record(s) at path via GET /data/:path.
  /// Supports wildcards '+' and '#' (auto URL-encoded).
  IoTStorageResponse getData(const char* path);

  /// Upsert a record at path via PUT /data/:path.
  /// Wildcards are NOT allowed in the path (server returns 400).
  IoTStorageResponse putData(const char* path, const JsonDocument& data);

  /// Delete record(s) at path via DELETE /data/:path.
  /// Supports wildcards '+' and '#'.
  IoTStorageResponse delData(const char* path);

  /// Health-check via GET /health.
  IoTStorageHealth health();

  /// Server info via GET /.
  IoTStorageServerInfo info();

  // ------------------------------------------------------------------
  // Asynchronous API — non-blocking, callback-based
  //
  // Call these in setup(). Call loop() repeatedly in your main loop().
  // Each callback fires when the response arrives.
  // Pending requests are processed one at a time in FIFO order.
  // ------------------------------------------------------------------

  typedef std::function<void(IoTStorageResponse&)> AsyncCallback;

  void queryAsync(const char* sql, AsyncCallback callback);
  void getDataAsync(const char* path, AsyncCallback callback);
  void putDataAsync(const char* path, const JsonDocument& data, AsyncCallback callback);
  void delDataAsync(const char* path, AsyncCallback callback);

  /// Must be called repeatedly (in loop()) to drive the async state machine.
  void loop();

  // ------------------------------------------------------------------
  // Convenience Methods — typed access to single-field records
  //
  // These use getData()/putData() under the hood.
  // ------------------------------------------------------------------

  /// Reads an int field from a record. Returns defaultValue if not found.
  int getInt(const char* path, const char* field, int defaultValue = 0);

  /// Reads a float field from a record. Returns defaultValue if not found.
  float getFloat(const char* path, const char* field, float defaultValue = 0.0f);

  /// Reads a string field from a record. Returns defaultValue if not found.
  /// The returned pointer is valid only until the next API call.
  const char* getString(const char* path, const char* field, const char* defaultValue = "");

  /// Reads a bool field from a record. Returns defaultValue if not found.
  bool getBool(const char* path, const char* field, bool defaultValue = false);

  /// Convenience: PUT an int value at path.
  bool putValue(const char* path, const char* field, int value);

  /// Convenience: PUT a float value at path.
  bool putValue(const char* path, const char* field, float value);

  /// Convenience: PUT a string value at path.
  bool putValue(const char* path, const char* field, const char* value);

  /// Convenience: PUT a bool value at path.
  bool putValue(const char* path, const char* field, bool value);

  // ------------------------------------------------------------------
  // Advanced — direct HTTP access
  // ------------------------------------------------------------------

  /// Send an arbitrary HTTP request to the server.
  /// Returns the raw response body as String (empty on failure).
  /// For internal use; exposed for advanced use cases.
  String httpRequest(const char* method, const char* path,
                     const char* body = nullptr);

  /// Get the host currently configured.
  const char* getHost() const;

  /// Get the port currently configured.
  uint16_t getPort() const;

private:
  // --- Configuration ---
  Client&  _client;
  String   _host;
  uint16_t _port;
  uint32_t _timeoutMs;
  bool     _serverSet;

  // --- Convenience value cache ---
  String   _stringCache;

  // --- Async state machine ---
  enum AsyncState {
    ASYNC_IDLE,
    ASYNC_RECEIVING_HEADERS,
    ASYNC_RECEIVING_BODY,
    ASYNC_COMPLETE
  };

  struct AsyncRequest {
    String         method;
    String         path;
    String         body;
    AsyncCallback  callback;
    unsigned long  startTime;
  };

  static const int MAX_QUEUE = 4;
  AsyncRequest _queue[MAX_QUEUE];
  int          _queueHead;
  int          _queueTail;
  AsyncState   _state;
  String       _responseBuffer;
  unsigned long _lastActivity;
  bool         _readingBody;
  size_t       _contentLength;
  bool         _chunked;
  IoTStorageResponse _asyncResponse;
  AsyncCallback _currentCallback;  // callback for the in-progress async request

  bool _enqueueRequest(const char* method, const char* path,
                       const char* body, AsyncCallback callback);
  bool _dequeueRequest(AsyncRequest& req);
  int  _queueCount() const;

  // --- Internal helpers ---
  String _urlEncodePath(const char* path);
};

// ============================================================================
// IoTStorageSingleton — Single-row settings wrapper around IoTStorageClient
//
// Designed for production use: define your settings fields, call load() to
// pull from the server, use setValue()/getXxx() to read/write, call save()
// to persist. load() distinguishes "record not found" (use defaults) from
// connection/operation errors.
// ============================================================================

class IoTStorageSingleton {
public:
  /// Result of a load() call.
  enum LoadStatus {
    LOAD_OK,        ///< Record exists on server — loaded successfully.
    LOAD_NOT_FOUND, ///< Record does NOT exist on server — set defaults & save().
    LOAD_ERROR      ///< Connection or server error — check client/server state.
  };

  /// Bind this singleton to a client and a data path (e.g. "/settings/mydevice").
  IoTStorageSingleton(IoTStorageClient& client, const char* path);

  /// Fetch the record from the server. Call this early in setup().
  /// - LOAD_OK        → record existed; fields are populated. Call getXxx().
  /// - LOAD_NOT_FOUND → no record on server. Set your defaults & call save().
  /// - LOAD_ERROR     → network or server problem. Inspect client/server.
  LoadStatus load();

  /// Persist the current in-memory document to the server (PUT /data/:path).
  /// Returns true on success, false on any error.
  bool save();

  // ---- Typed getters (read from the in-memory document) ----

  int         getInt(const char* field, int defaultValue = 0);
  float       getFloat(const char* field, float defaultValue = 0.0f);
  const char* getString(const char* field, const char* defaultValue = "");
  bool        getBool(const char* field, bool defaultValue = false);

  // ---- Typed setters (write to the in-memory document) ----

  void setValue(const char* field, int value);
  void setValue(const char* field, float value);
  void setValue(const char* field, const char* value);
  void setValue(const char* field, bool value);

  /// Direct access to the underlying JSON document for advanced use cases.
  JsonDocument& data();
  const JsonDocument& data() const;

  /// Quick existence check — performs a GET /data/:path to the server.
  /// Returns true if the record exists (200), false if 404 or connection error.
  bool exists();

  /// The configured data path (e.g. "/settings/mydevice").
  const char* path() const;

private:
  IoTStorageClient& _client;
  String            _path;
  JsonDocument      _data;
  String            _stringCache;
  bool              _loaded;
};

#endif // IOTSTORAGE_CLIENT_H
