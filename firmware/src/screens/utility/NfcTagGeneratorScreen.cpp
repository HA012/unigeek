#include "NfcTagGeneratorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NfcTagBuilder.h"

#if defined(ESP32)
#include <esp_system.h>
#endif

static String _sanitizeTagName(String name) {
  name.trim();
  if (name.length() == 0) name = "ntag215";

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);

  for (int i = 0; i < (int)name.length(); i++) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }

  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) name = "ntag215";
  return name;
}

const char* NfcTagGeneratorScreen::title() {
  switch (_state) {
    case STATE_TAG_TYPE:         return "Generate NFC Tag";
    case STATE_NDEF_CONTENT:     return "NDEF Content";
    case STATE_NDEF_TYPE:        return "New NDEF Record";
    case STATE_NDEF_FILE_SELECT: return "NDEF Files";
    case STATE_NDEF_PREVIEW:     return "NDEF Result";
  }
  return "Generate NFC Tag";
}

void NfcTagGeneratorScreen::onInit() {
  _goTagType();
}

void NfcTagGeneratorScreen::onUpdate() {
  if (_state == STATE_NDEF_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _openNdefFiles();
      } else if (dir == INavigation::DIR_PRESS && _previewNdefLen > 0) {
        if (_saveTag(_previewNdef, _previewNdefLen, _previewSuggestedName)) {
          _previewNdefLen = 0;
          _previewSuggestedName = "";
          _ndefPickDir = "";
          _goNdefContent();
        } else {
          render();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

void NfcTagGeneratorScreen::onRender() {
  if (_state == STATE_NDEF_PREVIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void NfcTagGeneratorScreen::onBack() {
  switch (_state) {
    case STATE_TAG_TYPE:
      Screen.goBack();
      break;

    case STATE_NDEF_CONTENT:
      _goTagType();
      break;

    case STATE_NDEF_TYPE:
      _goNdefContent();
      break;

    case STATE_NDEF_PREVIEW:
      _openNdefFiles();
      break;

    case STATE_NDEF_FILE_SELECT:
      if (_ndefPickDir == _nfcPath || _ndefPickDir.length() == 0) {
        _ndefPickDir = "";
        _goNdefContent();
      } else {
        int slash = _ndefPickDir.lastIndexOf('/');
        String parent = (slash > 0) ? _ndefPickDir.substring(0, slash) : String(_nfcPath);
        if (!parent.startsWith(_nfcPath)) parent = _nfcPath;
        _ndefPickDir = parent;
        _openNdefFiles();
      }
      break;
  }
}

void NfcTagGeneratorScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_TAG_TYPE:
      if (index == 0) {
        _tagType = TAG_MIFARE_CLASSIC_1K;
        _goNdefContent();
      } else if (index == 1) {
        _tagType = TAG_NTAG215;
        _goNdefContent();
      }
      break;

    case STATE_NDEF_CONTENT:
      if (index == 0) {
        _goNdefType();
      } else if (index == 1) {
        _ndefPickDir = _ndefPath;
        _openNdefFiles();
      } else if (index == 2) {
        _saveTag(nullptr, 0, _tagType == TAG_MIFARE_CLASSIC_1K ? "mfc1k_empty" : "ntag215_empty");
        render();
      }
      break;

    case STATE_NDEF_TYPE: {
      uint8_t ndef[NdefGeneratorScreen::MAX_NDEF_BYTES] = {};
      size_t ndefLen = 0;
      String suggestedName;
      if (NdefGeneratorScreen::buildRecordInteractive(
              index, ndef, ndefLen, sizeof(ndef), suggestedName)) {
        _saveTag(ndef, ndefLen, suggestedName);
      }
      render();
      break;
    }

    case STATE_NDEF_FILE_SELECT:
      _selectNdefFile(index);
      break;
  }
}

void NfcTagGeneratorScreen::_goTagType() {
  _state = STATE_TAG_TYPE;
  setItems(_tagItems);
}

void NfcTagGeneratorScreen::_goNdefContent() {
  _state = STATE_NDEF_CONTENT;
  setItems(_contentItems);
}

void NfcTagGeneratorScreen::_goNdefType() {
  _state = STATE_NDEF_TYPE;
  setItems(_ndefTypeItems);
}

void NfcTagGeneratorScreen::_openNdefFiles() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    _goNdefContent();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  if (_ndefPickDir.length() == 0) _ndefPickDir = _ndefPath;
  _browser.root = _nfcPath;
  _state = STATE_NDEF_FILE_SELECT;

  uint8_t n = _browser.load(this, _ndefPickDir, ".ndef");
  if (n == 0) {
    ShowStatusAction::show("No .ndef files", 1500);
    _ndefPickDir = "";
    _goNdefContent();
    return;
  }
  setItems(_browser.items(), n);
}

void NfcTagGeneratorScreen::_selectNdefFile(uint8_t index) {
  if (index >= _browser.count()) return;

  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _ndefPickDir = e.path;
    _openNdefFiles();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  const size_t len = f.size();
  if (len == 0 || len > MAX_INPUT_NDEF) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large", 1500);
    _openNdefFiles();
    return;
  }

  uint8_t ndef[MAX_INPUT_NDEF];
  const size_t got = f.read(ndef, len);
  f.close();
  if (got != len) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  String base = e.name;
  if (base.endsWith(".ndef")) base.remove(base.length() - 5);
  _showNdefPreview(ndef, len, base);
}


static String _tagPreviewNdefUriPrefix(uint8_t code) {
  switch (code) {
    case 0x00: return "";
    case 0x01: return "http://www.";
    case 0x02: return "https://www.";
    case 0x03: return "http://";
    case 0x04: return "https://";
    case 0x05: return "tel:";
    case 0x06: return "mailto:";
    case 0x07: return "ftp://anonymous:anonymous@";
    case 0x08: return "ftp://ftp.";
    case 0x09: return "ftps://";
    case 0x0A: return "sftp://";
    case 0x0B: return "smb://";
    case 0x0C: return "nfs://";
    case 0x0D: return "ftp://";
    case 0x0E: return "dav://";
    case 0x0F: return "news:";
    case 0x10: return "telnet://";
    case 0x11: return "imap:";
    case 0x12: return "rtsp://";
    case 0x13: return "urn:";
    case 0x14: return "pop:";
    case 0x15: return "sip:";
    case 0x16: return "sips:";
    case 0x17: return "tftp:";
    case 0x18: return "btspp://";
    case 0x19: return "btl2cap://";
    case 0x1A: return "btgoep://";
    case 0x1B: return "tcpobex://";
    case 0x1C: return "irdaobex://";
    case 0x1D: return "file://";
    case 0x1E: return "urn:epc:id:";
    case 0x1F: return "urn:epc:tag:";
    case 0x20: return "urn:epc:pat:";
    case 0x21: return "urn:epc:raw:";
    case 0x22: return "urn:epc:";
    case 0x23: return "urn:nfc:";
    default:   return "";
  }
}

static String _tagPreviewBytesToPrintable(const uint8_t* data, size_t len) {
  String s;
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    s += (c >= 32 && c <= 126) ? c : '.';
  }
  return s;
}

void NfcTagGeneratorScreen::_resetRows() { _rowCount = 0; }

void NfcTagGeneratorScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;
}

String NfcTagGeneratorScreen::_hexBlock(const uint8_t* data, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : " ", data[i]);
    s += buf;
  }
  return s;
}

void NfcTagGeneratorScreen::_pushWrappedRow(const String& label, const String& value) {
  if (value.length() == 0) {
    _pushRow(label, "");
    return;
  }

  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;
  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;
  while (pos < (int)value.length() && _rowCount < MAX_ROWS) {
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) pos++;
    if (pos >= (int)value.length()) break;

    const int maxChars = first ? firstChars : totalChars;
    int end = pos + maxChars;
    if (end > (int)value.length()) end = value.length();
    int newline = value.indexOf('\n', pos);
    if (newline >= pos && newline < end) {
      end = newline;
    } else if (end < (int)value.length()) {
      int split = -1;
      for (int i = end; i > pos; --i) {
        if (value[i - 1] == ' ' || value[i - 1] == '\t') { split = i - 1; break; }
      }
      if (split > pos) end = split;
    }
    if (end <= pos) end = min(pos + maxChars, (int)value.length());

    String part = value.substring(pos, end);
    part.trim();
    _pushRow(first ? label : "", part);
    pos = end;
    first = false;
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) pos++;
  }
}

void NfcTagGeneratorScreen::_showNdefPreview(const uint8_t* ndef, size_t ndefLen,
                                             const String& suggestedName) {
  _state = STATE_NDEF_PREVIEW;
  _resetRows();
  _previewNdefLen = 0;
  _previewSuggestedName = suggestedName;

  if (!ndef || ndefLen == 0 || ndefLen > MAX_INPUT_NDEF) {
    _pushRow("NDEF", "Invalid record");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  memcpy(_previewNdef, ndef, ndefLen);
  _previewNdefLen = ndefLen;

  char lenBuf[24];
  snprintf(lenBuf, sizeof(lenBuf), "%u bytes", (unsigned)ndefLen);
  _pushRow("NDEF Size", lenBuf);

  if (ndefLen < 3) {
    _pushRow("NDEF", "Invalid record");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  size_t p = 0;
  uint8_t flagsTnf = ndef[p++];
  bool sr = (flagsTnf & 0x10) != 0;
  bool il = (flagsTnf & 0x08) != 0;
  uint8_t tnf = flagsTnf & 0x07;

  auto finishPreview = [&]() {
    if (_previewNdefLen > 0) _pushRow("[Press]", "Generate Tag");
    _scrollView.setRows(_rows, _rowCount);
    render();
  };

  if (p >= ndefLen) {
    _pushRow("NDEF", "Invalid record");
    finishPreview();
    return;
  }
  uint8_t typeLen = ndef[p++];
  uint32_t payloadLen = 0;
  if (sr) {
    if (p >= ndefLen) {
    _pushRow("NDEF", "Invalid record");
    finishPreview();
    return;
  }
    payloadLen = ndef[p++];
  } else {
    if (p + 3 >= ndefLen) {
      _pushRow("NDEF", "Invalid record");
      finishPreview();
      return;
    }
    payloadLen = ((uint32_t)ndef[p] << 24) | ((uint32_t)ndef[p + 1] << 16) |
                 ((uint32_t)ndef[p + 2] << 8) | (uint32_t)ndef[p + 3];
    p += 4;
  }

  uint8_t idLen = 0;
  if (il) {
    if (p >= ndefLen) {
    _pushRow("NDEF", "Invalid record");
    finishPreview();
    return;
  }
    idLen = ndef[p++];
  }
  if (p + typeLen + idLen + payloadLen > ndefLen) {
    _pushRow("NDEF", "Truncated record");
    finishPreview();
    return;
  }

  {
    const uint8_t* type = &ndef[p];
    p += typeLen;
    p += idLen;
    const uint8_t* payload = &ndef[p];

    char tnfBuf[8];
    snprintf(tnfBuf, sizeof(tnfBuf), "%u", tnf);
    _pushRow("TNF", tnfBuf);
    String typeStr = _tagPreviewBytesToPrintable(type, typeLen);
    _pushRow("Type", typeStr.length() ? typeStr : "(empty)");

    if (tnf == 0x01 && typeLen == 1 && type[0] == 'T' && payloadLen >= 1) {
      uint8_t status = payload[0];
      bool utf16 = (status & 0x80) != 0;
      uint8_t langLen = status & 0x3F;
      if ((size_t)1 + langLen <= payloadLen) {
        String lang;
        for (uint8_t i = 0; i < langLen; i++) lang += (char)payload[1 + i];
        _pushRow("Record", "Text");
        _pushRow("Encoding", utf16 ? "UTF-16" : "UTF-8");
        if (lang.length()) _pushRow("Language", lang);
        if (!utf16) {
          String text;
          for (size_t i = 1 + langLen; i < payloadLen; i++) text += (char)payload[i];
          _pushWrappedRow("Text", text);
        } else {
          _pushRow("Text", "(UTF-16 raw)");
          uint8_t rawLen = (uint8_t)(payloadLen < 32 ? payloadLen : 32);
          _pushRow("Raw", _hexBlock(payload, rawLen));
        }
      }
    } else if (tnf == 0x01 && typeLen == 1 && type[0] == 'U' && payloadLen >= 1) {
      String uri = _tagPreviewNdefUriPrefix(payload[0]);
      for (size_t i = 1; i < payloadLen; i++) uri += (char)payload[i];
      if (uri.startsWith("mailto:")) {
        _pushRow("Record", "Email");
        _pushWrappedRow("Mail", uri.substring(7));
      } else if (uri.startsWith("tel:")) {
        _pushRow("Record", "Phone");
        _pushWrappedRow("Phone", uri.substring(4));
      } else {
        _pushRow("Record", "URI");
        _pushWrappedRow("URI", uri);
      }
    } else if (tnf == 0x02 &&
               (typeStr.equalsIgnoreCase("text/vcard") ||
                typeStr.equalsIgnoreCase("text/x-vcard"))) {
      String vcard;
      vcard.reserve(payloadLen);
      for (size_t i = 0; i < payloadLen; i++) vcard += (char)payload[i];
      vcard.replace("\r\n", "\n");
      vcard.replace("\r", "\n");
      _pushRow("Record", "vCard");

      auto vcardValue = [&](const char* key) -> String {
        int start = 0;
        while (start < (int)vcard.length()) {
          int end = vcard.indexOf('\n', start);
          if (end < 0) end = vcard.length();
          String line = vcard.substring(start, end);
          line.trim();
          int colon = line.indexOf(':');
          if (colon > 0) {
            String lhs = line.substring(0, colon);
            int semi = lhs.indexOf(';');
            if (semi >= 0) lhs = lhs.substring(0, semi);
            if (lhs.equalsIgnoreCase(key)) {
              String value = line.substring(colon + 1);
              value.replace("\\n", "\n");
              value.replace("\\,", ",");
              value.replace("\\;", ";");
              value.replace("\\\\", "\\");
              value.trim();
              return value;
            }
          }
          start = end + 1;
        }
        return "";
      };

      auto cleanStructured = [](String value) -> String {
        while (value.indexOf(";;") >= 0) value.replace(";;", ";");
        while (value.startsWith(";")) value.remove(0, 1);
        while (value.endsWith(";")) value.remove(value.length() - 1);
        value.replace(";", ", ");
        return value;
      };

      String name = vcardValue("FN");
      if (name.length() == 0) name = vcardValue("N");
      String company = vcardValue("ORG");
      String address = vcardValue("ADR");
      String phone = vcardValue("TEL");
      String mail = vcardValue("EMAIL");
      String website = vcardValue("URL");
      name = cleanStructured(name);
      company = cleanStructured(company);
      address = cleanStructured(address);

      if (name.length())    _pushWrappedRow("Contact name", name);
      if (company.length()) _pushWrappedRow("Company", company);
      if (address.length()) _pushWrappedRow("Address", address);
      if (phone.length())   _pushWrappedRow("Phone", phone);
      if (mail.length())    _pushWrappedRow("Mail", mail);
      if (website.length()) _pushWrappedRow("Website", website);
      if (!name.length() && !company.length() && !address.length() &&
          !phone.length() && !mail.length() && !website.length()) {
        _pushWrappedRow("vCard", vcard);
      }
    } else {
      _pushRow("Record", "Unsupported");
      uint8_t rawLen = (uint8_t)(payloadLen < 32 ? payloadLen : 32);
      _pushRow("Payload", _hexBlock(payload, rawLen));
    }
  }

  finishPreview();
}


bool NfcTagGeneratorScreen::_saveTag(const uint8_t* ndef, size_t ndefLen,
                                     const String& suggestedName) {
  if (_tagType == TAG_MIFARE_CLASSIC_1K) {
    return _saveMifareClassic1K(ndef, ndefLen, suggestedName);
  }
  return _saveNtag215(ndef, ndefLen, suggestedName);
}

void NfcTagGeneratorScreen::_generateMifareUid(uint8_t uid[4]) {
#if defined(ESP32)
  const uint32_t r = esp_random();
  uid[0] = (uint8_t)(r >> 0);
  uid[1] = (uint8_t)(r >> 8);
  uid[2] = (uint8_t)(r >> 16);
  uid[3] = (uint8_t)(r >> 24);
#else
  for (uint8_t i = 0; i < 4; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcTagGeneratorScreen::_saveMifareClassic1K(
    const uint8_t* ndef, size_t ndefLen, const String& suggestedName) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[4] = {};
  _generateMifareUid(uid);

  uint8_t image[NfcTagBuilder::MIFARE_CLASSIC_1K_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcTagBuilder::buildMifareClassic1K(
          uid, ndef, ndefLen, image, imageLen, sizeof(image))) {
    ShowStatusAction::show("Cannot build MFC1K", 1500);
    return false;
  }

  String name = InputTextAction::popup("File name", suggestedName.c_str());
  if (InputTextAction::wasCancelled()) return false;

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeTagName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}

void NfcTagGeneratorScreen::_generateUid(uint8_t uid[7]) {
  uid[0] = 0x04; // NXP manufacturer ID

#if defined(ESP32)
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uid[1] = (uint8_t)(r1 >> 0);
  uid[2] = (uint8_t)(r1 >> 8);
  uid[3] = (uint8_t)(r1 >> 16);
  uid[4] = (uint8_t)(r1 >> 24);
  uid[5] = (uint8_t)(r2 >> 0);
  uid[6] = (uint8_t)(r2 >> 8);
#else
  for (uint8_t i = 1; i < 7; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcTagGeneratorScreen::_saveNtag215(const uint8_t* ndef, size_t ndefLen,
                                         const String& suggestedName) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[7] = {};
  _generateUid(uid);

  uint8_t image[NfcTagBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcTagBuilder::buildNtag215(uid, ndef, ndefLen,
                                   image, imageLen, sizeof(image))) {
    ShowStatusAction::show("Cannot build NTAG215", 1500);
    return false;
  }

  String name = InputTextAction::popup("File name", suggestedName.c_str());
  if (InputTextAction::wasCancelled()) return false;

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeTagName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}
