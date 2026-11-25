#include "NtlmCrack.h"

#include <cstring>

// ── Little-endian helpers ──────────────────────────────────────────────────────
static inline uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
static inline uint32_t ld32le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void st32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// ── MD4 (RFC 1320) ─────────────────────────────────────────────────────────────
static void md4(const uint8_t* msg, size_t len, uint8_t out[16]) {
  uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
  // Padded length: message + 0x80 + zeros to 56 mod 64 + 8-byte bit length.
  const uint64_t bits = (uint64_t)len * 8;
  size_t total = len + 1;
  while (total % 64 != 56) ++total;
  total += 8;

  uint8_t block[64];
  for (size_t off = 0; off < total; off += 64) {
    for (int i = 0; i < 64; ++i) {
      size_t idx = off + i;
      if (idx < len)            block[i] = msg[idx];
      else if (idx == len)      block[i] = 0x80;
      else if (idx < total - 8) block[i] = 0x00;
      else                      block[i] = (uint8_t)(bits >> (8 * (idx - (total - 8))));
    }
    uint32_t x[16];
    for (int i = 0; i < 16; ++i) x[i] = ld32le(block + 4 * i);

    uint32_t aa = a, bb = b, cc = c, dd = d;
    auto F = [](uint32_t X, uint32_t Y, uint32_t Z) { return (X & Y) | (~X & Z); };
    auto G = [](uint32_t X, uint32_t Y, uint32_t Z) { return (X & Y) | (X & Z) | (Y & Z); };
    auto H = [](uint32_t X, uint32_t Y, uint32_t Z) { return X ^ Y ^ Z; };
#define MD4FF(A,B,C,D,K,S) A = rotl32(A + F(B,C,D) + x[K], S)
#define MD4GG(A,B,C,D,K,S) A = rotl32(A + G(B,C,D) + x[K] + 0x5a827999u, S)
#define MD4HH(A,B,C,D,K,S) A = rotl32(A + H(B,C,D) + x[K] + 0x6ed9eba1u, S)
    MD4FF(a,b,c,d, 0,3); MD4FF(d,a,b,c, 1,7); MD4FF(c,d,a,b, 2,11); MD4FF(b,c,d,a, 3,19);
    MD4FF(a,b,c,d, 4,3); MD4FF(d,a,b,c, 5,7); MD4FF(c,d,a,b, 6,11); MD4FF(b,c,d,a, 7,19);
    MD4FF(a,b,c,d, 8,3); MD4FF(d,a,b,c, 9,7); MD4FF(c,d,a,b,10,11); MD4FF(b,c,d,a,11,19);
    MD4FF(a,b,c,d,12,3); MD4FF(d,a,b,c,13,7); MD4FF(c,d,a,b,14,11); MD4FF(b,c,d,a,15,19);

    MD4GG(a,b,c,d, 0,3); MD4GG(d,a,b,c, 4,5); MD4GG(c,d,a,b, 8,9); MD4GG(b,c,d,a,12,13);
    MD4GG(a,b,c,d, 1,3); MD4GG(d,a,b,c, 5,5); MD4GG(c,d,a,b, 9,9); MD4GG(b,c,d,a,13,13);
    MD4GG(a,b,c,d, 2,3); MD4GG(d,a,b,c, 6,5); MD4GG(c,d,a,b,10,9); MD4GG(b,c,d,a,14,13);
    MD4GG(a,b,c,d, 3,3); MD4GG(d,a,b,c, 7,5); MD4GG(c,d,a,b,11,9); MD4GG(b,c,d,a,15,13);

    MD4HH(a,b,c,d, 0,3); MD4HH(d,a,b,c, 8,9); MD4HH(c,d,a,b, 4,11); MD4HH(b,c,d,a,12,15);
    MD4HH(a,b,c,d, 2,3); MD4HH(d,a,b,c,10,9); MD4HH(c,d,a,b, 6,11); MD4HH(b,c,d,a,14,15);
    MD4HH(a,b,c,d, 1,3); MD4HH(d,a,b,c, 9,9); MD4HH(c,d,a,b, 5,11); MD4HH(b,c,d,a,13,15);
    MD4HH(a,b,c,d, 3,3); MD4HH(d,a,b,c,11,9); MD4HH(c,d,a,b, 7,11); MD4HH(b,c,d,a,15,15);
#undef MD4FF
#undef MD4GG
#undef MD4HH
    a += aa; b += bb; c += cc; d += dd;
  }
  st32le(out + 0, a); st32le(out + 4, b); st32le(out + 8, c); st32le(out + 12, d);
}

// ── MD5 (RFC 1321) ─────────────────────────────────────────────────────────────
static const uint32_t MD5_K[64] = {
  0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
  0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
  0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
  0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
  0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
  0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
  0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
  0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
static const uint8_t MD5_S[64] = {
  7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
  5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
  4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
  6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21 };

static void md5(const uint8_t* msg, size_t len, uint8_t out[16]) {
  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  const uint64_t bits = (uint64_t)len * 8;
  size_t total = len + 1;
  while (total % 64 != 56) ++total;
  total += 8;

  uint8_t block[64];
  for (size_t off = 0; off < total; off += 64) {
    for (int i = 0; i < 64; ++i) {
      size_t idx = off + i;
      if (idx < len)            block[i] = msg[idx];
      else if (idx == len)      block[i] = 0x80;
      else if (idx < total - 8) block[i] = 0x00;
      else                      block[i] = (uint8_t)(bits >> (8 * (idx - (total - 8))));
    }
    uint32_t M[16];
    for (int i = 0; i < 16; ++i) M[i] = ld32le(block + 4 * i);

    uint32_t A = a0, B = b0, C = c0, D = d0;
    for (int i = 0; i < 64; ++i) {
      uint32_t Fv; int g;
      if      (i < 16) { Fv = (B & C) | (~B & D);        g = i; }
      else if (i < 32) { Fv = (D & B) | (~D & C);        g = (5 * i + 1) & 15; }
      else if (i < 48) { Fv = B ^ C ^ D;                 g = (3 * i + 5) & 15; }
      else             { Fv = C ^ (B | ~D);              g = (7 * i) & 15; }
      uint32_t tmp = D; D = C; C = B;
      B = B + rotl32(A + Fv + MD5_K[i] + M[g], MD5_S[i]);
      A = tmp;
    }
    a0 += A; b0 += B; c0 += C; d0 += D;
  }
  st32le(out + 0, a0); st32le(out + 4, b0); st32le(out + 8, c0); st32le(out + 12, d0);
}

// ── HMAC-MD5 ───────────────────────────────────────────────────────────────────
static void hmac_md5(const uint8_t* key, size_t klen,
                     const uint8_t* msg, size_t mlen, uint8_t out[16]) {
  uint8_t k[64] = {};
  if (klen > 64) { md5(key, klen, k); }         // keys never exceed 16 here, but be safe
  else           { memcpy(k, key, klen); }

  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

  // inner = MD5(ipad || msg)
  uint8_t ibuf[64 + 8 + 512];                   // ipad + (challenge8 + blob512) worst case
  memcpy(ibuf, ipad, 64);
  memcpy(ibuf + 64, msg, mlen);
  uint8_t inner[16];
  md5(ibuf, 64 + mlen, inner);

  // out = MD5(opad || inner)
  uint8_t obuf[64 + 16];
  memcpy(obuf, opad, 64);
  memcpy(obuf + 64, inner, 16);
  md5(obuf, 80, out);
}

// ── hex helpers ────────────────────────────────────────────────────────────────
static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
// Decode up to maxBytes from hex[.. first non-hex]. Returns bytes written, or -1
// if an odd nibble count or a bad char is hit before the delimiter.
static int hexDecode(const char* hex, size_t hexLen, uint8_t* out, size_t maxBytes) {
  if (hexLen % 2) return -1;
  size_t n = hexLen / 2;
  if (n > maxBytes) return -1;
  for (size_t i = 0; i < n; ++i) {
    int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return (int)n;
}

bool ntlm_parse_line(const char* line, NtlmHash& out) {
  out = NtlmHash{};
  if (!line) return false;

  // Trim leading blanks and compute the trimmed end (drop CR/LF/space).
  while (*line == ' ' || *line == '\t') ++line;
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                 line[len - 1] == ' '  || line[len - 1] == '\t')) --len;
  if (!len) return false;

  // Split on ':' — but the user/domain separator is "::", so field 0 is the
  // user, field 1 is empty, field 2 domain, 3 challenge, 4 ntproof, 5 blob.
  const char* fields[6] = {};
  size_t      flen[6]   = {};
  int nf = 0;
  const char* p = line;
  const char* end = line + len;
  const char* fstart = p;
  while (p <= end && nf < 6) {
    if (p == end || *p == ':') {
      fields[nf] = fstart;
      flen[nf]   = (size_t)(p - fstart);
      ++nf;
      fstart = p + 1;
      if (p == end) break;
    }
    ++p;
  }
  if (nf != 6) return false;          // need exactly the six colon-delimited parts
  if (flen[1] != 0) return false;     // the "::" must leave an empty field

  if (flen[0] == 0 || flen[0] >= sizeof(out.user))   return false;
  if (flen[2] >= sizeof(out.domain))                 return false;
  memcpy(out.user,   fields[0], flen[0]); out.user[flen[0]]   = '\0'; out.userLen   = (uint8_t)flen[0];
  memcpy(out.domain, fields[2], flen[2]); out.domain[flen[2]] = '\0'; out.domainLen = (uint8_t)flen[2];

  if (hexDecode(fields[3], flen[3], out.challenge, sizeof(out.challenge)) != 8)  return false;
  if (hexDecode(fields[4], flen[4], out.proof,     sizeof(out.proof))     != 16) return false;
  int bl = hexDecode(fields[5], flen[5], out.blob, sizeof(out.blob));
  if (bl <= 0) return false;
  out.blobLen = (uint16_t)bl;
  out.valid = true;
  return true;
}

void ntlm_prepare(const NtlmHash& h, NtlmTarget& t) {
  t = NtlmTarget{};
  t.h = &h;
  if (!h.valid) return;

  // id = UTF16LE( UPPER(user) + domain ); username upper-cased (ASCII), domain as-is.
  uint16_t n = 0;
  for (uint8_t i = 0; i < h.userLen && (size_t)(n + 2) <= sizeof(t.id); ++i) {
    char c = h.user[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    t.id[n++] = (uint8_t)c; t.id[n++] = 0x00;
  }
  for (uint8_t i = 0; i < h.domainLen && (size_t)(n + 2) <= sizeof(t.id); ++i) {
    t.id[n++] = (uint8_t)h.domain[i]; t.id[n++] = 0x00;
  }
  t.idLen = n;

  // chalblob = serverChallenge(8) || blob
  memcpy(t.chalblob, h.challenge, 8);
  uint16_t cb = 8;
  uint16_t copy = h.blobLen;
  if (copy > sizeof(t.chalblob) - 8) copy = sizeof(t.chalblob) - 8;
  memcpy(t.chalblob + 8, h.blob, copy);
  t.cbLen = cb + copy;
  t.valid = true;
}

bool fast_netntlmv2_try_password(const char* pw, uint8_t pwLen, const NtlmTarget& t) {
  if (!t.valid || !t.h) return false;

  // UTF16LE(password) by null-interleaving the raw bytes.
  uint8_t upw[2 * 64];
  if (pwLen > 64) pwLen = 64;
  uint16_t un = 0;
  for (uint8_t i = 0; i < pwLen; ++i) { upw[un++] = (uint8_t)pw[i]; upw[un++] = 0x00; }

  uint8_t ntlm[16];
  md4(upw, un, ntlm);

  uint8_t rk[16];
  hmac_md5(ntlm, 16, t.id, t.idLen, rk);

  uint8_t proof[16];
  hmac_md5(rk, 16, t.chalblob, t.cbLen, proof);

  return memcmp(proof, t.h->proof, 16) == 0;
}
