#include "IoTStorageClient.h"
#include <string.h>

// ============================================================================
// IoTStorageSingleton
// ============================================================================

IoTStorageSingleton::IoTStorageSingleton(IoTStorageClient& client, const char* path)
  : _client(client)
  , _path(path ? path : "")
  , _loaded(false)
{
}

const char* IoTStorageSingleton::path() const {
  return _path.c_str();
}

JsonDocument& IoTStorageSingleton::data() {
  return _data;
}

const JsonDocument& IoTStorageSingleton::data() const {
  return _data;
}

// ---------------------------------------------------------------------------
// load() — Fetch the record from the server
//
// Differentiates between:
//   LOAD_OK        = record exists, _data is populated
//   LOAD_NOT_FOUND = 404 on the server → caller sets defaults & calls save()
//   LOAD_ERROR     = connection or server error (5xx, timeout, etc.)
// ---------------------------------------------------------------------------

IoTStorageSingleton::LoadStatus IoTStorageSingleton::load() {
  if (_path.length() == 0) return LOAD_ERROR;

  IoTStorageResponse resp = _client.getData(_path.c_str());

  if (!resp.success()) {
    // 404 means the record does not exist yet — distinct from errors
    if (resp.httpStatus() == 404) {
      _data.clear();
      _loaded = true;
      return LOAD_NOT_FOUND;
    }
    // Any other failure (timeout, 5xx, etc.)
    _loaded = false;
    return LOAD_ERROR;
  }

  // Success — copy the returned JSON into our local document
  _data.clear();

  // The server may return the record directly (no "rows" wrapper) for single-path GET
  if (!resp.json()["_path"].isNull() && resp.json()["rows"].isNull()) {
    // Single record returned inline — copy all fields except _path, _type
    JsonObjectConst src = resp.json();
    for (JsonPairConst kv : src) {
      const char* key = kv.key().c_str();
      if (strcmp(key, "_path") != 0 &&
          strcmp(key, "_type") != 0 &&
          strcmp(key, "_created") != 0 &&
          strcmp(key, "_updated") != 0) {
        _data[key] = kv.value();
      }
    }
  } else if (resp.rows().size() > 0) {
    // Wrapped in "rows" array — take first row
    JsonObjectConst src = resp.row(0);
    for (JsonPairConst kv : src) {
      const char* key = kv.key().c_str();
      if (strcmp(key, "_path") != 0 &&
          strcmp(key, "_type") != 0 &&
          strcmp(key, "_created") != 0 &&
          strcmp(key, "_updated") != 0) {
        _data[key] = kv.value();
      }
    }
  }
  // else: success with empty body — treat as empty record

  _loaded = true;
  return LOAD_OK;
}

// ---------------------------------------------------------------------------
// save() — Persist the in-memory document to the server
// ---------------------------------------------------------------------------

bool IoTStorageSingleton::save() {
  if (_path.length() == 0) return false;

  // Merge _path into the document for the server
  _data["_path"] = _path;

  IoTStorageResponse resp = _client.putData(_path.c_str(), _data);

  // Remove _path so the user's document stays clean
  _data.remove("_path");

  _loaded = resp.success();
  return _loaded;
}

// ---------------------------------------------------------------------------
// exists() — Quick server-side existence check
// ---------------------------------------------------------------------------

bool IoTStorageSingleton::exists() {
  if (_path.length() == 0) return false;

  IoTStorageResponse resp = _client.getData(_path.c_str());

  // 200 with success=true means it exists
  if (resp.success()) return true;

  // 404 means definitely not found
  if (resp.httpStatus() == 404) return false;

  // Connection error — can't determine
  return false;
}

// ---------------------------------------------------------------------------
// Typed getters — read from the local _data document
// ---------------------------------------------------------------------------

int IoTStorageSingleton::getInt(const char* field, int defaultValue) {
  if (!field || !_loaded) return defaultValue;
  if (_data[field].is<int>()) return _data[field].as<int>();
  return defaultValue;
}

float IoTStorageSingleton::getFloat(const char* field, float defaultValue) {
  if (!field || !_loaded) return defaultValue;
  if (_data[field].is<float>() || _data[field].is<int>()) return _data[field].as<float>();
  return defaultValue;
}

const char* IoTStorageSingleton::getString(const char* field, const char* defaultValue) {
  if (!field || !_loaded) return defaultValue;
  if (_data[field].is<const char*>()) {
    _stringCache = _data[field].as<String>();
    return _stringCache.c_str();
  }
  return defaultValue;
}

bool IoTStorageSingleton::getBool(const char* field, bool defaultValue) {
  if (!field || !_loaded) return defaultValue;
  if (_data[field].is<bool>()) return _data[field].as<bool>();
  if (_data[field].is<int>())  return _data[field].as<int>() != 0;
  return defaultValue;
}

// ---------------------------------------------------------------------------
// Typed setters — write to the local _data document
// ---------------------------------------------------------------------------

void IoTStorageSingleton::setValue(const char* field, int value) {
  if (!field) return;
  _data[field] = value;
  _loaded = true;
}

void IoTStorageSingleton::setValue(const char* field, float value) {
  if (!field) return;
  _data[field] = value;
  _loaded = true;
}

void IoTStorageSingleton::setValue(const char* field, const char* value) {
  if (!field) return;
  _data[field] = value;
  _loaded = true;
}

void IoTStorageSingleton::setValue(const char* field, bool value) {
  if (!field) return;
  _data[field] = value;
  _loaded = true;
}
