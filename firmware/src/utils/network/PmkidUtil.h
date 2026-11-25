#pragma once

#include <cstdint>

// ── PMKID helpers (clientless WPA2) ─────────────────────────────────────────
// Shared by WifiUnigotchiScreen (opportunistic, while roaming) and
// WifiPmkidCaptureScreen (interactive, targeted). Factored here so the active
// probe and the PMKID detection live in one place.
namespace PmkidUtil {

// Send an 802.11 association request carrying an RSN IE (PSK/CCMP) to `bssid`,
// sourced from `staMac` (a spoofed, locally-administered STA). A WPA2 AP that
// supports PMKSA caching answers with an EAPOL M1 containing a PMKID — no
// associated client required. Transmitted on the current radio channel via
// WIFI_IF_AP, so the caller must have an AP interface up (e.g. WifiAttackUtil).
void sendAssocRequest(const uint8_t* bssid, const uint8_t* staMac,
                      const char* ssid, uint8_t ssidLen);

// True when a raw 802.11 frame is an EAPOL-Key message carrying a non-zero
// PMKID KDE (RSN, OUI 00-0F-AC type 4). Operates on the whole frame; find the
// LLC/SNAP + 0x888E, walk to the key data, and scan for the PMKID element.
bool hasPmkid(const uint8_t* data, uint16_t len);

}  // namespace PmkidUtil
