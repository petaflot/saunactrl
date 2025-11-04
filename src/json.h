#ifndef JSON_H
#define JSON_H

#include <Arduino.h>

bool verifyJsonHMAC(const String &json, const char *secret);
bool verifyQueryHMAC(const String &qs, const char *secret);

#endif

