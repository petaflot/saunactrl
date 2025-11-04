/* helpers */
function buf2hex(buf) {
  return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
}

/* set cookie (not HttpOnly) */
function setSecretCookie(name, secret, days=7) {
  const expires = new Date(Date.now() + days*864e5).toUTCString();
  // secure flag recommended when using https
  document.cookie = `${encodeURIComponent(name)}=${encodeURIComponent(secret)}; expires=${expires}; path=/; Secure`;
}

function getCookie(name) {
  return document.cookie.split('; ').reduce((acc, kv) => {
    const [k,v] = kv.split('=');
    return k === encodeURIComponent(name) ? decodeURIComponent(v) : acc;
  }, null);
}

/* HMAC-SHA256 using SubtleCrypto */
async function hmacSha256Hex(message, secret) {
  // secret: string
  const enc = new TextEncoder();
  const keyData = enc.encode(secret);
  const key = await crypto.subtle.importKey(
    "raw", keyData, {name: "HMAC", hash: "SHA-256"}, false, ["sign"]
  );
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(message));
  return buf2hex(sig);
}

function buildCanonical(obj) {
  const keys = Object.keys(obj).filter(k => k !== "hmac").sort();
  return keys.map(k => `${k}=${obj[k]}`).join("&");
}

async function signData(obj, secretKey) {
  const canonical = buildCanonical(obj);

  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secretKey),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );

  const signature = await crypto.subtle.sign("HMAC", key, enc.encode(canonical));
  const hex = Array.from(new Uint8Array(signature))
                   .map(b => b.toString(16).padStart(2, "0"))
                   .join("");
  return hex;
}
