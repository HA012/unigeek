#include "IpScanUtil.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip_addr.h>
#include "DnsUtil.h"

// ── ICMP internals ───────────────────────────────────────────────────────────

static constexpr uint16_t PING_ID = 0x1A2B;
static volatile bool s_pingReceived = false;

static uint8_t onPingRecv(void* /*arg*/, struct raw_pcb* /*pcb*/,
                          struct pbuf* p, const ip_addr_t* /*addr*/) {
  // pbuf payload starts at the IP header
  if (p->len >= 20) {
    uint8_t ihl = (((uint8_t*)p->payload)[0] & 0x0F) * 4;
    uint16_t icmpOffset = ihl;
    if (p->len >= icmpOffset + (uint16_t)sizeof(struct icmp_echo_hdr)) {
      auto* iecho = (struct icmp_echo_hdr*)((uint8_t*)p->payload + icmpOffset);
      if (ICMPH_TYPE(iecho) == ICMP_ER && ntohs(iecho->id) == PING_ID) {
        s_pingReceived = true;
      }
    }
  }
  pbuf_free(p);
  return 1;  // consumed
}

static bool icmpPing(const char* ipStr, uint32_t timeoutMs) {
  ip_addr_t dest;
  if (!ipaddr_aton(ipStr, &dest)) return false;

  struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return false;

  s_pingReceived = false;
  raw_recv(pcb, onPingRecv, nullptr);

  struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
  if (!p) { raw_remove(pcb); return false; }

  auto* iecho = (struct icmp_echo_hdr*)p->payload;
  memset(iecho, 0, sizeof(*iecho));
  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->id     = htons(PING_ID);
  iecho->seqno  = htons(1);
  iecho->chksum = inet_chksum(iecho, sizeof(struct icmp_echo_hdr));

  raw_sendto(pcb, p, &dest);
  pbuf_free(p);

  uint32_t start = millis();
  while (!s_pingReceived && (millis() - start) < timeoutMs) delay(5);

  raw_remove(pcb);
  return s_pingReceived;
}

// ── NetBIOS name resolution ─────────────────────────────────────────────────

static void encodeNetbiosName(const uint8_t rawName[16], uint8_t encoded[34]) {
  encoded[0] = 32;
  for (uint8_t i = 0; i < 16; i++) {
    encoded[1 + i * 2] = 'A' + ((rawName[i] >> 4) & 0x0F);
    encoded[2 + i * 2] = 'A' + (rawName[i] & 0x0F);
  }
  encoded[33] = 0;
}

static bool netbiosResolve(const char* ipStr, char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  IPAddress target;
  if (!target.fromString(ipStr)) return false;

  WiFiUDP udp;
  if (!udp.begin(0)) return false;

  uint8_t packet[50] = {0};
  uint16_t txid = (uint16_t)(millis() & 0xFFFF);
  packet[0] = txid >> 8;
  packet[1] = txid & 0xFF;
  packet[4] = 0;
  packet[5] = 1;  // one question

  uint8_t wildcard[16] = {0};
  wildcard[0] = '*';
  uint8_t encoded[34];
  encodeNetbiosName(wildcard, encoded);
  memcpy(packet + 12, encoded, sizeof(encoded));
  packet[46] = 0x00;
  packet[47] = 0x21;  // NBSTAT
  packet[48] = 0x00;
  packet[49] = 0x01;  // IN

  if (!udp.beginPacket(target, 137)) {
    udp.stop();
    return false;
  }
  udp.write(packet, sizeof(packet));
  if (!udp.endPacket()) {
    udp.stop();
    return false;
  }

  uint32_t start = millis();
  int packetSize = 0;
  while ((millis() - start) < 250) {
    packetSize = udp.parsePacket();
    if (packetSize > 0) break;
    delay(5);
  }

  if (packetSize <= 0) {
    udp.stop();
    return false;
  }

  uint8_t response[576];
  int len = udp.read(response, sizeof(response));
  udp.stop();
  if (len < 12 || response[0] != packet[0] || response[1] != packet[1]) return false;

  // Skip the DNS-like header, question name/type/class, then the answer name.
  size_t pos = 12;
  if (pos >= (size_t)len) return false;
  if (response[pos] == 0x20) pos += 34;
  else return false;
  if (pos + 4 > (size_t)len) return false;
  pos += 4; // QTYPE + QCLASS

  if (pos + 2 > (size_t)len) return false;
  if ((response[pos] & 0xC0) == 0xC0) pos += 2;
  else {
    while (pos < (size_t)len && response[pos] != 0) {
      uint8_t labelLen = response[pos++];
      if (pos + labelLen > (size_t)len) return false;
      pos += labelLen;
    }
    if (pos >= (size_t)len) return false;
    pos++;
  }

  if (pos + 10 > (size_t)len) return false;
  uint16_t type = ((uint16_t)response[pos] << 8) | response[pos + 1];
  pos += 8; // TYPE + CLASS + TTL
  uint16_t rdLength = ((uint16_t)response[pos] << 8) | response[pos + 1];
  pos += 2;
  if (type != 0x0021 || pos + rdLength > (size_t)len || rdLength < 1) return false;

  uint8_t nameCount = response[pos++];
  for (uint8_t i = 0; i < nameCount; i++) {
    if (pos + 18 > (size_t)len) break;

    const uint8_t* entry = response + pos;
    uint8_t suffix = entry[15];
    uint16_t flags = ((uint16_t)entry[16] << 8) | entry[17];
    bool groupName = (flags & 0x8000) != 0;

    if (suffix == 0x00 && !groupName) {
      size_t nameLen = 15;
      while (nameLen > 0 && entry[nameLen - 1] == ' ') nameLen--;
      if (nameLen > 0) {
        size_t copyLen = (nameLen < outLen - 1) ? nameLen : outLen - 1;
        memcpy(out, entry, copyLen);
        out[copyLen] = '\0';
        return true;
      }
    }
    pos += 18;
  }

  return false;
}

// ── IpScanUtil public methods ────────────────────────────────────────────────

bool IpScanUtil::resolveName(const char* ip, char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = '\0';

  DnsUtil::resolveHostname(ip, out, outLen);
  if (out[0] != '\0') return true;

  return netbiosResolve(ip, out, outLen);
}

bool IpScanUtil::scanTarget(const char* targetIp, Host& out, bool resolveHostnames) {
  memset(&out, 0, sizeof(out));
  if (!targetIp || !icmpPing(targetIp, 100)) return false;

  strncpy(out.ip, targetIp, sizeof(out.ip) - 1);
  out.ip[sizeof(out.ip) - 1] = '\0';
  if (resolveHostnames) resolveName(targetIp, out.hostname, sizeof(out.hostname));
  return true;
}

uint8_t IpScanUtil::scan(uint8_t startOctet, uint8_t endOctet,
                         Host* out, uint8_t maxHosts,
                         bool resolveHostnames,
                         void (*progressCb)(uint8_t)) {
  if (!out || maxHosts == 0 || startOctet > endOctet) return 0;

  IPAddress localIP = WiFi.localIP();
  if (localIP == IPAddress(0, 0, 0, 0)) return 0;

  char baseIp[16];
  snprintf(baseIp, sizeof(baseIp), "%d.%d.%d.", localIP[0], localIP[1], localIP[2]);

  int total = endOctet - startOctet + 1;
  uint8_t found = 0;

  for (int i = startOctet; i <= endOctet && found < maxHosts; i++) {
    if (progressCb) progressCb((uint8_t)((i - startOctet) * 100 / total));
    if (i == (int)localIP[3]) continue;  // skip self

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%s%d", baseIp, i);

    if (icmpPing(ipStr, 100)) {
      strncpy(out[found].ip, ipStr, sizeof(out[found].ip) - 1);
      out[found].ip[sizeof(out[found].ip) - 1] = '\0';
      out[found].hostname[0] = '\0';
      if (resolveHostnames) resolveName(ipStr, out[found].hostname, sizeof(out[found].hostname));
      found++;
    }
  }

  if (progressCb) progressCb(100);
  return found;
}
