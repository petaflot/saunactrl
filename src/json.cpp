#include <Arduino.h>

// TODO: more precision in floats! (at least 2 decimals, possibly configurable)
// TODO: return true/false instead of 1.000 or 0.000 when applicable
class JsonBuilder {
public:
  JsonBuilder() { clear(); }

  void clear() {
    pos = snprintf(buffer, sizeof(buffer), "{");
    first = true;
  }

  // --- Single numeric or enum ---
  template <typename T>
  typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type
  addValue(const char *key, T value) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "\"%s\":%.3f", key, static_cast<double>(value));
    first = false;
  }

  // --- Single string ---
  void addValue(const char *key, const char *value) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "\"%s\":\"%s\"", key, value);
    first = false;
  }
  void addValue(const char *key, const String &value) { addValue(key, value.c_str()); }

  // --- Array of numeric/enums ---
  template <typename T, size_t N>
  void addValue(const char *key, T (&arr)[N]) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\"%s\":[", key);
    for (size_t i = 0; i < N; i++) {
      pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                      (i < N - 1) ? "%d," : "%d", static_cast<int>(arr[i]));
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "]");
    first = false;
  }

  // --- Array of floats from function pointer ---
  void addValue(const char *key, size_t count, float (*func)(size_t)) {
    if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\"%s\":[", key);
    for (size_t i = 0; i < count; i++) {
      float val = func(i);
      pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                      (i < count - 1) ? "%.2f," : "%.2f", val);
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "]");
    first = false;
  }

  const char* finish() {
    snprintf(buffer + pos, sizeof(buffer) - pos, "}");
    return buffer;
  }

  bool hasValues() const {
    return !first;  // false if nothing added yet
  }

private:
  char buffer[256];
  size_t pos;
  bool first;
};

