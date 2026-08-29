#include "utils/network/SsdpScanUtil.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <string.h>

namespace {

const IPAddress SSDP_MCAST(239, 255, 255, 250);
constexpr uint16_t SSDP_PORT = 1900;

const char* ciStrStr(const char* haystack, const char* needle)
{
  if (!haystack || !needle || !*needle) return haystack;
  size_t nl = strlen(needle);
  for (const char* p = haystack; *p; ++p) {
    if (strncasecmp(p, needle, nl) == 0) return p;
  }
  return nullptr;
}

bool extractHeader(const char* packet, const char* header, char* out, size_t outLen)
{
  if (!packet || !header || !out || outLen == 0) return false;
  out[0] = '\0';

  const char* p = ciStrStr(packet, header);
  if (!p) return false;
  p += strlen(header);
  while (*p == ' ' || *p == '\t') ++p;

  size_t n = 0;
  while (*p && *p != '\r' && *p != '\n' && n + 1 < outLen) {
    out[n++] = *p++;
  }
  out[n] = '\0';
  return n > 0;
}

String extractTag(const String& body, const char* tag)
{
  String openTag  = String("<") + tag + ">";
  String closeTag = String("</") + tag + ">";

  int s = body.indexOf(openTag);
  if (s < 0) return "";
  s += openTag.length();

  int e = body.indexOf(closeTag, s);
  if (e < 0) return "";

  String value = body.substring(s, e);
  value.trim();
  return value;
}

void fetchFriendlyName(SsdpScanUtil::Device& dev)
{
  if (!dev.location[0]) return;

  HTTPClient http;
  http.setTimeout(1800);
  if (!http.begin(dev.location)) return;

  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    String name = extractTag(body, "friendlyName");
    if (name.isEmpty()) name = extractTag(body, "deviceName");
    if (name.isEmpty()) name = extractTag(body, "modelName");

    if (!name.isEmpty()) {
      strncpy(dev.name, name.c_str(), sizeof(dev.name) - 1);
      dev.name[sizeof(dev.name) - 1] = '\0';
    }
  }
  http.end();
}

} // namespace

uint8_t SsdpScanUtil::discover(const char* searchTarget,
                               Device* out,
                               uint8_t maxDevices,
                               void (*progressCb)(uint8_t))
{
  if (!searchTarget || !*searchTarget || !out || maxDevices == 0) return 0;
  if (WiFi.status() != WL_CONNECTED) return 0;

  WiFiUDP udp;
  if (!udp.begin(0)) return 0;

  char request[256];
  snprintf(request, sizeof(request),
           "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: %s\r\n"
           "\r\n",
           searchTarget);

  auto sendSearch = [&]() {
    if (!udp.beginPacket(SSDP_MCAST, SSDP_PORT)) return;
    udp.write((const uint8_t*)request, strlen(request));
    udp.endPacket();
  };

  // Match the proven CastBomb/PrinterPrank discovery strategy: multicast
  // responses are lossy, so send the query three times during the window.
  sendSearch();
  uint8_t searchesSent = 1;
  const uint8_t maxSearches = 3;
  const uint32_t resendEveryMs = 900;

  uint8_t count = 0;
  uint32_t startMs = millis();
  const uint32_t totalMs = 4500;
  uint32_t nextResend = startMs + resendEveryMs;

  while ((uint32_t)(millis() - startMs) < totalMs && count < maxDevices) {
    if (searchesSent < maxSearches &&
        (int32_t)(millis() - nextResend) >= 0) {
      sendSearch();
      searchesSent++;
      nextResend = millis() + resendEveryMs;
    }

    if (progressCb) {
      uint32_t elapsed = millis() - startMs;
      uint8_t pct = elapsed >= totalMs
                      ? 100
                      : (uint8_t)(elapsed * 100 / totalMs);
      progressCb(pct);
    }

    int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(40);
      continue;
    }

    char packet[1024];
    int len = udp.read(packet, sizeof(packet) - 1);
    if (len <= 0) continue;
    packet[len] = '\0';

    IPAddress src = udp.remoteIP();
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
             src[0], src[1], src[2], src[3]);

    // Generic SSDP discovery is device-oriented here: a host may answer
    // ssdp:all for many service/device types, but appears once in the UI.
    bool duplicate = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (strcmp(out[i].ip, ip) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    Device& dev = out[count];
    memset(&dev, 0, sizeof(dev));

    strncpy(dev.ip, ip, sizeof(dev.ip) - 1);
    strncpy(dev.name, "SSDP Device", sizeof(dev.name) - 1);

    extractHeader(packet, "ST:",       dev.st,       sizeof(dev.st));
    extractHeader(packet, "USN:",      dev.usn,      sizeof(dev.usn));
    extractHeader(packet, "SERVER:",   dev.server,   sizeof(dev.server));
    extractHeader(packet, "LOCATION:", dev.location, sizeof(dev.location));

    fetchFriendlyName(dev);
    count++;
  }

  udp.stop();
  if (progressCb) progressCb(100);
  return count;
}
