#ifndef HMAC_H
#define HMAC_H

#include <Arduino.h>
#include "sha256.h"

// ---- Low-level helpers ----

// Convert binary to lowercase hex string (out buffer must be 2*len+1)
void bin_to_hex_lower(const uint8_t *in, size_t len, char *out);

// Compute HMAC-SHA256(secret, message), return lowercase hex string
String compute_hmac_hex(const char *secret, const String &message);

// Case-insensitive hex comparison (constant-time)
bool hex_equals_ci(const String &a, const String &b);

// ---- Verification helpers ----

// Verify HMAC for a JSON string ({"a":1,"b":2,"hmac":"..."})
bool verifyJsonHMAC(const String &json, const char *secret);

// Verify HMAC for a query string (a=1&b=2&hmac=...)
bool verifyQueryHMAC(const String &qs, const char *secret);

// If AsyncWebServer is used: verify GET/POST params
#ifdef ASYNCWEBSERVER_H_INCLUDED
#include <ESPAsyncWebServer.h>
bool verifyRequestHMAC(AsyncWebServerRequest *request, const char *secret);
#endif

#endif // HMAC_H

