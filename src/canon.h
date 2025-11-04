#ifndef CANON_H
#define CANON_H

#include <Arduino.h>

struct KV {
  String key;
  String val;
};

#define MAX_KV 32

// Parse "a=1&b=2&hmac=..." → KV array (excluding hmac)
size_t parseQueryString(const String &qs, KV out[], size_t maxItems, String &hmacOut);

// Parse JSON string → KV array (excluding hmac)
size_t parseJsonToKVs(const String &json, KV out[], size_t maxItems, String &hmacOut);

// Sort KV array lexicographically by key
void sortKVs(KV arr[], size_t n);

// Build canonical "k1=v1&k2=v2" string
String buildCanonical(KV arr[], size_t n);

#endif // CANON_H

