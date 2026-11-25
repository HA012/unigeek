#pragma once

#include <cstdint>
#include <cstddef>

// ── NetNTLMv2 (hashcat -m 5600) offline cracker ────────────────────────────────
// Verifies a password against an NTLMv2 challenge/response captured by Responder.
// Pure C++ (no Arduino / mbedtls) so it is host-testable and, like FastWpaCrack,
// lock-free — the MD4/MD5/HMAC-MD5 here touch no shared state, so the same
// primitives run on both cores at once with no mutex.
//
// Per candidate:
//   ntlm   = MD4(UTF16LE(password))
//   rkNT   = HMAC-MD5(ntlm,  UTF16LE(UPPER(user) + domain))
//   proof' = HMAC-MD5(rkNT,  serverChallenge(8) || blob)
//   match  = proof' == captured NTProofStr(16)
// (NTLMv2 upper-cases the username but not the domain; the domain is used as-is.)

struct NtlmHash {
  char     user[64]   = {};
  uint8_t  userLen    = 0;
  char     domain[64] = {};
  uint8_t  domainLen  = 0;
  uint8_t  challenge[8] = {};
  uint8_t  proof[16]    = {};   // NTProofStr
  uint8_t  blob[512]    = {};
  uint16_t blobLen      = 0;
  bool     valid        = false;
};

// Parse one hashcat -m 5600 line "user::domain:challenge:ntproof:blob".
// Trailing CR/LF and surrounding blanks are tolerated. Returns false (and
// out.valid == false) on any malformed field.
bool ntlm_parse_line(const char* line, NtlmHash& out);

// Per-hash constants, computed once so the per-password path is three hashes:
//   id      = UTF16LE(UPPER(user) + domain)   (HMAC-MD5 message for rkNT)
//   chalblob = serverChallenge(8) || blob     (HMAC-MD5 message for proof')
struct NtlmTarget {
  const NtlmHash* h       = nullptr;
  uint8_t  id[256]        = {};
  uint16_t idLen          = 0;
  uint8_t  chalblob[8 + 512] = {};
  uint16_t cbLen          = 0;
  bool     valid          = false;
};

void ntlm_prepare(const NtlmHash& h, NtlmTarget& t);

// Test one password (raw bytes, treated as UTF16LE by null-interleaving, which
// matches hashcat for ASCII/latin1 wordlists). Returns true if it matches.
bool fast_netntlmv2_try_password(const char* pw, uint8_t pwLen, const NtlmTarget& t);
