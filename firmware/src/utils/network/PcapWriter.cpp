#include "utils/network/PcapWriter.h"

#include "core/Device.h"
#include "ui/actions/ShowStatusAction.h"

#include <time.h>

// Epoch for 2020-01-01 — anything below means the clock was never set.
static constexpr time_t kClockSane = 1577836800L;

#pragma pack(push, 1)
struct PcapGlobalHdr {
  uint32_t magic = 0xA1B2C3D4;
  uint16_t vmaj  = 2;
  uint16_t vmin  = 4;
  int32_t  tz    = 0;
  uint32_t sig   = 0;
  uint32_t snap;
  uint32_t linktype;
};
struct PcapPktHdr {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
};
#pragma pack(pop)

String PcapWriter::sanitize(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.') {
      out += c;
    } else {
      out += '_';
    }
  }
  return out;
}

bool PcapWriter::resolveDate(char* out, size_t n) {
  time_t now = time(nullptr);

  if (now < kClockSane) {
    ShowStatusAction::show("Syncing time (NTP)...", 0);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    const unsigned long start = millis();
    while (millis() - start < 6000) {
      now = time(nullptr);
      if (now >= kClockSane) break;
      // Keep servicing the device while we block, so input and the power
      // manager do not go unattended for six seconds.
      Uni.update();
      delay(100);
    }
  }

  if (now < kClockSane) {
    snprintf(out, n, "00-00-0000");
    return false;
  }

  struct tm t;
  gmtime_r(&now, &t);          // configTime(0, 0, ...) keeps the clock in UTC
  strftime(out, n, "%m-%d-%Y", &t);
  return true;
}

bool PcapWriter::begin(IStorage* fs, const char* dir, const String& ssid,
                       uint32_t linktype, uint32_t snapLen) {
  end();

  _fs    = fs;
  _error = nullptr;
  _len   = 0;
  _frames = 0;
  _bytes  = 0;

  if (!_fs || !_fs->isAvailable()) { _error = "SD card not available"; return false; }

  if (!_buf) {
    _buf = (uint8_t*)malloc(WBUF_SIZE);
    if (!_buf) { _error = "Out of memory"; return false; }
  }

  char date[16];
  const bool timeOk = resolveDate(date, sizeof(date));

  String name = sanitize(ssid);
  if (name.length() == 0) name = "unknown";
  _filename = name + "_" + date + ".pcap";

  _fs->makeDir("/unigeek");
  _fs->makeDir("/unigeek/wifi");
  _fs->makeDir(dir);

  const String path = String(dir) + "/" + _filename;
  const bool resuming = _fs->exists(path.c_str());

  _file = _fs->open(path.c_str(), resuming ? FILE_APPEND : FILE_WRITE);
  if (!_file) { _error = "Failed to open capture file"; return false; }

  if (!resuming) {
    PcapGlobalHdr hdr;
    hdr.snap     = snapLen;
    hdr.linktype = linktype;
    if (_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
      _file.close();
      _error = "Failed to write to SD";
      return false;
    }
  }

  _bytes     = _file.size();
  _baseEpoch = timeOk ? (uint32_t)time(nullptr) : 0;
  _baseMs    = millis();
  _ok        = true;
  return true;
}

void PcapWriter::end() {
  if (_file) {
    flush();
    _file.flush();
    _file.close();
  }
  if (_buf) { free(_buf); _buf = nullptr; }
  _ok  = false;
  _len = 0;
}

bool PcapWriter::writeFrame(const uint8_t* data, uint16_t len, uint16_t origLen,
                            uint32_t tsSec, uint32_t tsUsec) {
  if (!_ok || !_buf) return false;

  PcapPktHdr ph;
  ph.ts_sec   = tsSec;
  ph.ts_usec  = tsUsec;
  ph.incl_len = len;
  ph.orig_len = origLen;

  if (_len + sizeof(ph) + len > WBUF_SIZE) {
    if (!flush()) return false;
  }
  // A single frame larger than the whole stage buffer would loop forever above.
  if (sizeof(ph) + len > WBUF_SIZE) return false;

  memcpy(_buf + _len, &ph, sizeof(ph));
  _len += sizeof(ph);
  memcpy(_buf + _len, data, len);
  _len += len;

  _frames++;
  _bytes += sizeof(ph) + len;
  return true;
}

bool PcapWriter::flush() {
  if (_len == 0 || !_buf) return true;
  if (!_file) { _ok = false; _error = "Failed to write to SD"; return false; }

  const size_t want = _len;
  const size_t n    = _file.write(_buf, want);
  _len = 0;

  // A short write means the card is full or failing. A truncated record would
  // desync every frame after it, so stop rather than corrupt the capture.
  if (n != want) {
    _ok    = false;
    _error = "Failed to write to SD";
    return false;
  }
  return true;
}
