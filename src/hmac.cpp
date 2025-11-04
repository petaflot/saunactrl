#include "hmac.h"

void bin_to_hex_lower(const uint8_t *in, size_t len, char *out) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i=0;i<len;i++) {
    out[i*2]   = hex[(in[i] >> 4) & 0xF];
    out[i*2+1] = hex[in[i] & 0xF];
  }
  out[len*2] = '\0';
}

String compute_hmac_hex(const char *secret, const String &message) {
  uint8_t mac[SHA256_HASH_SIZE];
  hmac_sha256((const uint8_t*)secret, strlen(secret),
              (const uint8_t*)message.c_str(), message.length(),
              mac);
  char hexbuf[SHA256_HASH_SIZE*2 + 1];
  bin_to_hex_lower(mac, SHA256_HASH_SIZE, hexbuf);
  return String(hexbuf);
}

bool hex_equals_ci(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  for (size_t i=0;i<a.length();i++) {
    char ca = a.charAt(i);
    char cb = b.charAt(i);
    if (ca >= 'A' && ca <= 'F') ca = ca - 'A' + 'a';
    if (cb >= 'A' && cb <= 'F') cb = cb - 'A' + 'a';
    if (ca != cb) return false;
  }
  return true;
}

