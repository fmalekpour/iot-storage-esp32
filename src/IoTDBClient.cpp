#include "IoTDBClient.h"
#include <string.h>

// ============================================================================
// IoTDBResponse
// ============================================================================

IoTDBResponse::IoTDBResponse()
  : _success(false)
  , _httpStatus(0)
  , _rowCount(0)
  , _affectedRows(0)
{
}

bool IoTDBResponse::success() const {
  return _success;
}

const char* IoTDBResponse::errorMessage() const {
  return _errorMessage.c_str();
}

int IoTDBResponse::httpStatus() const {
  return _httpStatus;
}

const char* IoTDBResponse::responseType() const {
  return _responseType.c_str();
}

int IoTDBResponse::count() const {
  return _rowCount;
}

int IoTDBResponse::affected() const {
  return _affectedRows;
}

JsonDocument& IoTDBResponse::json() {
  return _json;
}

const JsonDocument& IoTDBResponse::json() const {
  return _json;
}

JsonArrayConst IoTDBResponse::rows() const {
  return _json["rows"].as<JsonArrayConst>();
}

JsonObjectConst IoTDBResponse::row(int index) const {
  JsonArrayConst arr = _json["rows"].as<JsonArrayConst>();
  if (index >= 0 && index < (int)arr.size()) {
    return arr[index].as<JsonObjectConst>();
  }
  return JsonObjectConst();
}

void IoTDBResponse::_setError(const char* msg) {
  _success = false;
  _errorMessage = msg ? msg : "";
}

void IoTDBResponse::_setHttpStatus(int code) {
  _httpStatus = code;
}

void IoTDBResponse::_parse(const String& responseBody) {
  _json.clear();

  if (responseBody.length() == 0) {
    _success = (_httpStatus >= 200 && _httpStatus < 300);
    if (!_success) {
      _errorMessage = "Empty response from server";
    }
    return;
  }

  DeserializationError err = deserializeJson(_json, responseBody);
  if (err) {
    _success = false;
    _errorMessage = "JSON parse error: ";
    _errorMessage += err.c_str();
    return;
  }

  // Check for server-side error: { "error": "..." }
  if (_json["error"].is<const char*>()) {
    _success = false;
    _errorMessage = _json["error"].as<String>();
    return;
  }

  _success = (_httpStatus >= 200 && _httpStatus < 300);

  if (!_success) {
    _errorMessage = "HTTP error ";
    _errorMessage += _httpStatus;
    return;
  }

  if (_json["type"].is<const char*>()) {
    _responseType = _json["type"].as<String>();
  }
  if (_json["count"].is<int>()) {
    _rowCount = _json["count"].as<int>();
  }
  if (_json["affected"].is<int>()) {
    _affectedRows = _json["affected"].as<int>();
  }
}


// ============================================================================
// IoTDBClient — Construction & Configuration
// ============================================================================

IoTDBClient::IoTDBClient(Client& client)
  : _client(client)
  , _port(9123)
  , _timeoutMs(5000)
  , _serverSet(false)
  , _queueHead(0)
  , _queueTail(0)
  , _state(ASYNC_IDLE)
  , _lastActivity(0)
  , _readingBody(false)
  , _contentLength(0)
  , _chunked(false)
{
}

void IoTDBClient::setServer(const char* host, uint16_t port) {
  _host = host ? host : "";
  _port = port;
  _serverSet = (host != nullptr && strlen(host) > 0);
}

void IoTDBClient::setTimeout(uint32_t ms) {
  _timeoutMs = ms;
}

uint32_t IoTDBClient::timeout() const {
  return _timeoutMs;
}

const char* IoTDBClient::getHost() const {
  return _host.c_str();
}

uint16_t IoTDBClient::getPort() const {
  return _port;
}


// ============================================================================
// URL Path Encoding
// ============================================================================

String IoTDBClient::_urlEncodePath(const char* path) {
  String result;
  result.reserve(strlen(path) + 16);

  for (const char* p = path; *p; p++) {
    char c = *p;
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~' ||
        c == '/') {
      result += c;
    } else if (c == '#') {
      result += "%23";
    } else if (c == '+') {
      result += "%2B";
    } else if (c == ' ') {
      result += "%20";
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      result += hex;
    }
  }

  return result;
}


// ============================================================================
// Internal: low-level HTTP round-trip
// ============================================================================

static String _httpRoundTrip(Client& client,
                              const char* host, uint16_t port,
                              const char* method, const char* path,
                              const char* body,
                              uint32_t timeoutMs,
                              int& httpCode) {
  httpCode = 0;

  if (!client.connect(host, port)) {
    return "";
  }

  client.setTimeout(timeoutMs);

  // --- Send request ---
  client.print(method);
  client.print(" ");
  client.print(path);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.println("User-Agent: IoTDBClient/1.0.0 (Arduino)");
  client.println("Connection: close");
  client.println("Accept: application/json");

  if (body && strlen(body) > 0) {
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(strlen(body));
    client.println();
    client.print(body);
  } else {
    client.println();
  }

  client.flush();

  // --- Read status line ---
  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  if (statusLine.startsWith("HTTP/")) {
    int sp1 = statusLine.indexOf(' ');
    if (sp1 > 0) {
      httpCode = statusLine.substring(sp1 + 1, sp1 + 4).toInt();
    }
  }

  // --- Read headers ---
  bool chunked = false;
  size_t contentLength = 0;

  while (true) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;

    if (line.startsWith("Transfer-Encoding:") &&
        line.indexOf("chunked") > 0) {
      chunked = true;
    } else if (line.startsWith("Content-Length:")) {
      String val = line.substring(strlen("Content-Length:"));
      val.trim();
      contentLength = val.toInt();
    }
  }

  // --- Read body ---
  String responseBody;

  if (chunked) {
    unsigned long chunkStart = millis();
    while (millis() - chunkStart < timeoutMs) {
      // Read chunk size line
      String sizeLine;
      while (client.available()) {
        char c = client.read();
        if (c == '\n') break;
        if (c != '\r') sizeLine += c;
      }
      sizeLine.trim();
      long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSize <= 0) break;

      size_t read = 0;
      while (read < (size_t)chunkSize) {
        if (client.available()) {
          responseBody += (char)client.read();
          read++;
        } else {
          delay(1);
          if (millis() - chunkStart >= timeoutMs) break;
        }
      }

      // Read trailing \r\n
      while (client.available()) {
        char c = client.read();
        if (c == '\n') break;
      }
    }
    // Discard trailing headers
    while (client.available()) client.read();

  } else if (contentLength > 0) {
    unsigned long bodyStart = millis();
    while (responseBody.length() < contentLength &&
           (millis() - bodyStart < timeoutMs)) {
      while (client.available() && responseBody.length() < contentLength) {
        responseBody += (char)client.read();
      }
      if (responseBody.length() < contentLength) delay(1);
    }

  } else {
    // No length, no chunking — read until close
    unsigned long lastData = millis();
    while (millis() - lastData < timeoutMs) {
      while (client.available()) {
        responseBody += (char)client.read();
        lastData = millis();
      }
      delay(1);
    }
  }

  client.stop();
  return responseBody;
}


// ============================================================================
// Synchronous API
// ============================================================================

IoTDBResponse IoTDBClient::query(const char* sql) {
  IoTDBResponse resp;

  if (!_serverSet || _host.length() == 0) {
    resp._setError("Server not configured. Call setServer() first.");
    return resp;
  }

  if (!sql || strlen(sql) == 0) {
    resp._setError("SQL query string is empty");
    return resp;
  }

  JsonDocument bodyDoc;
  bodyDoc["sql"] = sql;
  String jsonBody;
  serializeJson(bodyDoc, jsonBody);

  int httpCode = 0;
  String rawBody = _httpRoundTrip(_client, _host.c_str(), _port,
                                   "POST", "/query", jsonBody.c_str(),
                                   _timeoutMs, httpCode);

  if (rawBody.length() == 0 && httpCode == 0) {
    resp._setError("Connection failed or timed out");
    resp._setHttpStatus(0);
    return resp;
  }

  resp._setHttpStatus(httpCode);
  resp._parse(rawBody);
  return resp;
}

IoTDBResponse IoTDBClient::getData(const char* path) {
  IoTDBResponse resp;

  if (!_serverSet || _host.length() == 0) {
    resp._setError("Server not configured. Call setServer() first.");
    return resp;
  }

  String encodedPath = _urlEncodePath(path);
  String routePath = "/data";
  routePath += encodedPath;

  int httpCode = 0;
  String rawBody = _httpRoundTrip(_client, _host.c_str(), _port,
                                   "GET", routePath.c_str(), nullptr,
                                   _timeoutMs, httpCode);

  if (rawBody.length() == 0 && httpCode == 0) {
    resp._setError("Connection failed or timed out");
    resp._setHttpStatus(0);
    return resp;
  }

  resp._setHttpStatus(httpCode);
  resp._parse(rawBody);
  return resp;
}

IoTDBResponse IoTDBClient::putData(const char* path, const JsonDocument& data) {
  IoTDBResponse resp;

  if (!_serverSet || _host.length() == 0) {
    resp._setError("Server not configured. Call setServer() first.");
    return resp;
  }

  String encodedPath = _urlEncodePath(path);
  String routePath = "/data";
  routePath += encodedPath;

  String jsonBody;
  serializeJson(data, jsonBody);

  int httpCode = 0;
  String rawBody = _httpRoundTrip(_client, _host.c_str(), _port,
                                   "PUT", routePath.c_str(), jsonBody.c_str(),
                                   _timeoutMs, httpCode);

  if (rawBody.length() == 0 && httpCode == 0) {
    resp._setError("Connection failed or timed out");
    resp._setHttpStatus(0);
    return resp;
  }

  resp._setHttpStatus(httpCode);
  resp._parse(rawBody);
  return resp;
}

IoTDBResponse IoTDBClient::delData(const char* path) {
  IoTDBResponse resp;

  if (!_serverSet || _host.length() == 0) {
    resp._setError("Server not configured. Call setServer() first.");
    return resp;
  }

  String encodedPath = _urlEncodePath(path);
  String routePath = "/data";
  routePath += encodedPath;

  int httpCode = 0;
  String rawBody = _httpRoundTrip(_client, _host.c_str(), _port,
                                   "DELETE", routePath.c_str(), nullptr,
                                   _timeoutMs, httpCode);

  if (rawBody.length() == 0 && httpCode == 0) {
    resp._setError("Connection failed or timed out");
    resp._setHttpStatus(0);
    return resp;
  }

  resp._setHttpStatus(httpCode);
  resp._parse(rawBody);
  return resp;
}

IoTDBHealth IoTDBClient::health() {
  IoTDBHealth h;
  h.ok       = false;
  h.uptime   = 0;
  h.backend  = "";
  h.version  = "";

  if (!_serverSet || _host.length() == 0) return h;

  int httpCode = 0;
  String body = _httpRoundTrip(_client, _host.c_str(), _port,
                                "GET", "/health", nullptr,
                                _timeoutMs, httpCode);

  if (body.length() == 0) return h;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return h;

  if (doc["status"].is<const char*>()) {
    h.ok = (strcmp(doc["status"].as<const char*>(), "ok") == 0);
  }
  if (doc["uptime"].is<long>()) {
    h.uptime = doc["uptime"].as<long>();
  }
  _stringCache = "";
  if (doc["backend"].is<const char*>()) {
    _stringCache = doc["backend"].as<String>();
    h.backend = _stringCache.c_str();
  }
  if (doc["version"].is<const char*>()) {
    static String verCache;
    verCache = doc["version"].as<String>();
    h.version = verCache.c_str();
  }
  return h;
}

IoTDBServerInfo IoTDBClient::info() {
  IoTDBServerInfo inf;
  inf.name    = "";
  inf.version = "";

  if (!_serverSet || _host.length() == 0) return inf;

  int httpCode = 0;
  String body = _httpRoundTrip(_client, _host.c_str(), _port,
                                "GET", "/", nullptr,
                                _timeoutMs, httpCode);

  if (body.length() == 0) return inf;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return inf;

  if (doc["name"].is<const char*>()) {
    static String nameCache;
    nameCache = doc["name"].as<String>();
    inf.name = nameCache.c_str();
  }
  if (doc["version"].is<const char*>()) {
    static String verCache;
    verCache = doc["version"].as<String>();
    inf.version = verCache.c_str();
  }
  return inf;
}

String IoTDBClient::httpRequest(const char* method, const char* path,
                                 const char* body) {
  int httpCode = 0;
  return _httpRoundTrip(_client, _host.c_str(), _port,
                         method, path, body,
                         _timeoutMs, httpCode);
}


// ============================================================================
// Asynchronous API — Queue + State Machine
// ============================================================================

bool IoTDBClient::_enqueueRequest(const char* method, const char* path,
                                   const char* body, AsyncCallback callback) {
  if (_queueCount() >= MAX_QUEUE) {
    return false;
  }

  AsyncRequest& req = _queue[_queueTail];
  req.method    = method;
  req.path      = path;
  req.body      = body ? body : "";
  req.callback  = callback;

  _queueTail = (_queueTail + 1) % MAX_QUEUE;
  return true;
}

bool IoTDBClient::_dequeueRequest(AsyncRequest& req) {
  if (_queueCount() == 0) return false;

  req = _queue[_queueHead];
  _queueHead = (_queueHead + 1) % MAX_QUEUE;
  return true;
}

int IoTDBClient::_queueCount() const {
  return (_queueTail - _queueHead + MAX_QUEUE) % MAX_QUEUE;
}

void IoTDBClient::queryAsync(const char* sql, AsyncCallback callback) {
  if (!sql || strlen(sql) == 0) {
    if (callback) {
      IoTDBResponse resp;
      resp._setError("SQL query string is empty");
      callback(resp);
    }
    return;
  }

  JsonDocument bodyDoc;
  bodyDoc["sql"] = sql;
  String jsonBody;
  serializeJson(bodyDoc, jsonBody);

  if (!_enqueueRequest("POST", "/query", jsonBody.c_str(), callback)) {
    if (callback) {
      IoTDBResponse resp;
      resp._setError("Async queue full (max 4 pending requests)");
      callback(resp);
    }
  }
}

void IoTDBClient::getDataAsync(const char* path, AsyncCallback callback) {
  if (!_enqueueRequest("GET", path, nullptr, callback)) {
    if (callback) {
      IoTDBResponse resp;
      resp._setError("Async queue full (max 4 pending requests)");
      callback(resp);
    }
  }
}

void IoTDBClient::putDataAsync(const char* path, const JsonDocument& data,
                                AsyncCallback callback) {
  String jsonBody;
  serializeJson(data, jsonBody);

  if (!_enqueueRequest("PUT", path, jsonBody.c_str(), callback)) {
    if (callback) {
      IoTDBResponse resp;
      resp._setError("Async queue full (max 4 pending requests)");
      callback(resp);
    }
  }
}

void IoTDBClient::delDataAsync(const char* path, AsyncCallback callback) {
  if (!_enqueueRequest("DELETE", path, nullptr, callback)) {
    if (callback) {
      IoTDBResponse resp;
      resp._setError("Async queue full (max 4 pending requests)");
      callback(resp);
    }
  }
}

void IoTDBClient::loop() {
  switch (_state) {

    case ASYNC_IDLE: {
      if (_queueCount() == 0) break;

      AsyncRequest req;
      if (!_dequeueRequest(req)) break;

      if (!_serverSet || _host.length() == 0) {
        _asyncResponse._setError("Server not configured. Call setServer() first.");
        _asyncResponse._setHttpStatus(0);
        if (req.callback) req.callback(_asyncResponse);
        break;
      }

      String encodedPath = _urlEncodePath(req.path.c_str());
      String routePath;
      if (strcmp(req.method.c_str(), "POST") == 0) {
        routePath = req.path;
      } else {
        routePath = "/data";
        routePath += encodedPath;
      }

      if (!_client.connect(_host.c_str(), _port)) {
        _asyncResponse._setError("Connection failed");
        _asyncResponse._setHttpStatus(0);
        if (req.callback) req.callback(_asyncResponse);
        break;
      }

      _client.setTimeout(_timeoutMs);

      _client.print(req.method);
      _client.print(" ");
      _client.print(routePath);
      _client.println(" HTTP/1.1");

      _client.print("Host: ");
      _client.println(_host);
      _client.println("User-Agent: IoTDBClient/1.0.0 (Arduino)");
      _client.println("Connection: close");
      _client.println("Accept: application/json");

      if (req.body.length() > 0) {
        _client.println("Content-Type: application/json");
        _client.print("Content-Length: ");
        _client.println(req.body.length());
        _client.println();
        _client.print(req.body);
      } else {
        _client.println();
      }

      _client.flush();

      // Store callback for completion
      _currentCallback = req.callback;

      _responseBuffer = "";
      _responseBuffer.reserve(4096);
      _readingBody   = false;
      _contentLength = 0;
      _chunked       = false;
      _lastActivity  = millis();
      _state         = ASYNC_RECEIVING_HEADERS;
      break;
    }

    case ASYNC_RECEIVING_HEADERS: {
      while (_client.available()) {
        String line = _client.readStringUntil('\n');
        _lastActivity = millis();

        if (line.endsWith("\r")) {
          line.remove(line.length() - 1);
        }

        if (line.length() == 0) {
          _readingBody = true;
          if (_chunked || _contentLength > 0) {
            _state = ASYNC_RECEIVING_BODY;
          } else {
            _state = ASYNC_COMPLETE;
          }
          return;
        }

        if (line.startsWith("Content-Length:")) {
          String val = line.substring(strlen("Content-Length:"));
          val.trim();
          _contentLength = val.toInt();
        } else if (line.startsWith("Transfer-Encoding:") &&
                   line.indexOf("chunked") > 0) {
          _chunked = true;
        }
      }

      if (millis() - _lastActivity > _timeoutMs) {
        _asyncResponse._setError("Timeout receiving headers");
        _asyncResponse._setHttpStatus(0);
        _client.stop();
        _state = ASYNC_COMPLETE;
      }
      break;
    }

    case ASYNC_RECEIVING_BODY: {
      if (_chunked) {
        while (_client.available()) {
          String sizeLine = _client.readStringUntil('\n');
          sizeLine.trim();
          long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
          if (chunkSize <= 0) {
            while (_client.available()) _client.read();
            _state = ASYNC_COMPLETE;
            _client.stop();
            return;
          }

          size_t read = 0;
          unsigned long chunkStart = millis();
          while (read < (size_t)chunkSize) {
            if (_client.available()) {
              _responseBuffer += (char)_client.read();
              read++;
              _lastActivity = millis();
            } else {
              if (millis() - chunkStart > _timeoutMs) {
                _asyncResponse._setError("Timeout reading chunked body");
                _asyncResponse._setHttpStatus(0);
                _client.stop();
                _state = ASYNC_COMPLETE;
                return;
              }
              delay(1);
            }
          }

          while (_client.available()) {
            char c = _client.read();
            if (c == '\n') break;
          }
        }

        if (millis() - _lastActivity > _timeoutMs) {
          _asyncResponse._setError("Timeout waiting for chunked data");
          _asyncResponse._setHttpStatus(0);
          _client.stop();
          _state = ASYNC_COMPLETE;
        }
      } else if (_contentLength > 0) {
        while (_client.available() &&
               _responseBuffer.length() < _contentLength) {
          _responseBuffer += (char)_client.read();
          _lastActivity = millis();
        }

        if (_responseBuffer.length() >= _contentLength) {
          _state = ASYNC_COMPLETE;
          _client.stop();
        } else if (millis() - _lastActivity > _timeoutMs) {
          _asyncResponse._setError("Timeout reading body");
          _asyncResponse._setHttpStatus(0);
          _client.stop();
          _state = ASYNC_COMPLETE;
        }
      } else {
        _state = ASYNC_COMPLETE;
      }
      break;
    }

    case ASYNC_COMPLETE: {
      _asyncResponse = IoTDBResponse();
      _asyncResponse._setHttpStatus(200);
      _asyncResponse._parse(_responseBuffer);

      AsyncCallback cb = _currentCallback;
      _currentCallback = nullptr;
      if (cb) {
        cb(_asyncResponse);
      }

      _client.stop();
      _responseBuffer = "";
      _state = ASYNC_IDLE;
      break;
    }

    default:
      break;
  }
}


// ============================================================================
// Convenience Methods
// ============================================================================

int IoTDBClient::getInt(const char* path, const char* field, int defaultValue) {
  IoTDBResponse resp = getData(path);
  if (!resp.success()) return defaultValue;

  if (!resp.json()["_path"].isNull() && resp.json()["rows"].isNull()) {
    if (resp.json()[field].is<int>()) {
      return resp.json()[field].as<int>();
    }
  } else if (resp.rows().size() > 0) {
    JsonObjectConst obj = resp.row(0);
    if (obj[field].is<int>()) {
      return obj[field].as<int>();
    }
  }
  return defaultValue;
}

float IoTDBClient::getFloat(const char* path, const char* field, float defaultValue) {
  IoTDBResponse resp = getData(path);
  if (!resp.success()) return defaultValue;

  if (!resp.json()["_path"].isNull() && resp.json()["rows"].isNull()) {
    if (resp.json()[field].is<float>() || resp.json()[field].is<int>()) {
      return resp.json()[field].as<float>();
    }
  } else if (resp.rows().size() > 0) {
    JsonObjectConst obj = resp.row(0);
    if (obj[field].is<float>() || obj[field].is<int>()) {
      return obj[field].as<float>();
    }
  }
  return defaultValue;
}

const char* IoTDBClient::getString(const char* path, const char* field,
                                    const char* defaultValue) {
  IoTDBResponse resp = getData(path);
  if (!resp.success()) return defaultValue;

  if (!resp.json()["_path"].isNull() && resp.json()["rows"].isNull()) {
    if (resp.json()[field].is<const char*>()) {
      _stringCache = resp.json()[field].as<String>();
      return _stringCache.c_str();
    }
  } else if (resp.rows().size() > 0) {
    JsonObjectConst obj = resp.row(0);
    if (obj[field].is<const char*>()) {
      _stringCache = obj[field].as<String>();
      return _stringCache.c_str();
    }
  }
  return defaultValue;
}

bool IoTDBClient::getBool(const char* path, const char* field, bool defaultValue) {
  IoTDBResponse resp = getData(path);
  if (!resp.success()) return defaultValue;

  if (!resp.json()["_path"].isNull() && resp.json()["rows"].isNull()) {
    if (resp.json()[field].is<bool>()) {
      return resp.json()[field].as<bool>();
    }
    // Also accept 0/1 int as bool
    if (resp.json()[field].is<int>()) {
      return resp.json()[field].as<int>() != 0;
    }
  } else if (resp.rows().size() > 0) {
    JsonObjectConst obj = resp.row(0);
    if (obj[field].is<bool>()) {
      return obj[field].as<bool>();
    }
    if (obj[field].is<int>()) {
      return obj[field].as<int>() != 0;
    }
  }
  return defaultValue;
}

bool IoTDBClient::putValue(const char* path, const char* field, int value) {
  JsonDocument doc;
  doc[field] = value;
  IoTDBResponse resp = putData(path, doc);
  return resp.success();
}

bool IoTDBClient::putValue(const char* path, const char* field, float value) {
  JsonDocument doc;
  doc[field] = value;
  IoTDBResponse resp = putData(path, doc);
  return resp.success();
}

bool IoTDBClient::putValue(const char* path, const char* field, const char* value) {
  JsonDocument doc;
  doc[field] = value;
  IoTDBResponse resp = putData(path, doc);
  return resp.success();
}

bool IoTDBClient::putValue(const char* path, const char* field, bool value) {
  JsonDocument doc;
  doc[field] = value;
  IoTDBResponse resp = putData(path, doc);
  return resp.success();
}
