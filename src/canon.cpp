#include "canon.h"
#include <ArduinoJson.h>

size_t parseQueryString(const String &qs, KV out[], size_t maxItems, String &hmacOut) {
  hmacOut = "";
  size_t count = 0;
  size_t start = 0;
  while (start < qs.length()) {
    int amp = qs.indexOf('&', start);
    if (amp == -1) amp = qs.length();
    String pair = qs.substring(start, amp);
    int eq = pair.indexOf('=');
    if (eq >= 0) {
      String k = pair.substring(0, eq);
      String v = pair.substring(eq+1);
      if (k.equalsIgnoreCase("hmac")) {
        hmacOut = v;
      } else if (count < maxItems) {
        out[count].key = k;
        out[count].val = v;
        count++;
      }
    }
    start = (amp == (int)qs.length()) ? qs.length() : amp + 1;
  }
  return count;
}

size_t parseJsonToKVs(const String &json, KV out[], size_t maxItems, String &hmacOut) {
  hmacOut = "";
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return 0;

  size_t count = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    String k = String(kv.key().c_str());
    if (k.equalsIgnoreCase("hmac")) {
      hmacOut = kv.value().as<String>();
    } else if (count < maxItems) {
      if (kv.value().is<bool>()) {
        out[count].val = kv.value().as<bool>() ? "true" : "false";
      } else if (kv.value().is<long>() || kv.value().is<int>()) {
        out[count].val = String(kv.value().as<long>());
      } else if (kv.value().is<float>() || kv.value().is<double>()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6g", kv.value().as<double>());
        out[count].val = buf;
      } else {
        out[count].val = kv.value().as<String>();
      }
      out[count].key = k;
      count++;
    }
  }
  return count;
}

void sortKVs(KV arr[], size_t n) {
  for (size_t i=0; i<n; i++) {
    size_t min = i;
    for (size_t j=i+1; j<n; j++) {
      if (arr[j].key < arr[min].key) min = j;
    }
    if (min != i) {
      KV tmp = arr[i];
      arr[i] = arr[min];
      arr[min] = tmp;
    }
  }
}

String buildCanonical(KV arr[], size_t n) {
  String out;
  for (size_t i=0; i<n; i++) {
    if (i > 0) out += "&";
    out += arr[i].key;
    out += "=";
    out += arr[i].val;
  }
  return out;
}

