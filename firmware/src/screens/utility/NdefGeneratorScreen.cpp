#include "NdefGeneratorScreen.h"
#include "core/Device.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NdefBuilder.h"

static String _sanitizeNdefName(String name) {
  name.trim();
  if (name.length() == 0) name = "record";

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
  if (name.length() == 0) name = "record";
  return name;
}

void NdefGeneratorScreen::onInit() {
  setItems(_items);
}

void NdefGeneratorScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _generateText();  break;
    case 1: _generateUrl();   break;
    case 2: _generatePhone(); break;
    case 3: _generateEmail(); break;
    case 4: _generateVcard(); break;
  }
}

bool NdefGeneratorScreen::_saveNdef(const uint8_t* ndef, size_t ndefLen,
                                    const String& suggestedName) {
  if (!ndef || ndefLen == 0 || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("Invalid NDEF record");
    return false;
  }

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    return false;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  const String base = _sanitizeNdefName(suggestedName);
  String path = String(_ndefPath) + "/" + base + ".ndef";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_ndefPath) + "/" + base + "_(" + n + ").ndef";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed");
    return false;
  }

  const size_t written = f.write(ndef, ndefLen);
  f.close();

  if (written != ndefLen) {
    ShowStatusAction::show("Save failed");
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String name = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + name).c_str(), 1500);
  return true;
}

void NdefGeneratorScreen::_generateText() {
  String value = InputTextAction::popup("Text", "");
  if (InputTextAction::wasCancelled() || value.length() == 0) { render(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildText(value, ndef, ndefLen, MAX_NDEF_BYTES)) {
    ShowStatusAction::show("Text too long");
    render();
    return;
  }

  String name = InputTextAction::popup("File name", "text");
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}

void NdefGeneratorScreen::_generateUrl() {
  String url = InputTextAction::popup("URL", "https://");
  if (InputTextAction::wasCancelled() || url.length() == 0) { render(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildUrl(url, ndef, ndefLen, MAX_NDEF_BYTES)) {
    ShowStatusAction::show("URL too long");
    render();
    return;
  }

  String name = InputTextAction::popup("File name", "url");
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}

void NdefGeneratorScreen::_generatePhone() {
  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled() || phone.length() == 0) { render(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildPhone(phone, ndef, ndefLen, MAX_NDEF_BYTES)) {
    ShowStatusAction::show("Phone too long");
    render();
    return;
  }

  String name = InputTextAction::popup("File name", "phone");
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}

void NdefGeneratorScreen::_generateEmail() {
  String email = InputTextAction::popup("Email", "");
  if (InputTextAction::wasCancelled() || email.length() == 0) { render(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildEmail(email, ndef, ndefLen, MAX_NDEF_BYTES)) {
    ShowStatusAction::show("Email too long");
    render();
    return;
  }

  String name = InputTextAction::popup("File name", "email");
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}

void NdefGeneratorScreen::_generateVcard() {
  String contact = InputTextAction::popup("Contact name", "");
  if (InputTextAction::wasCancelled() || contact.length() == 0) { render(); return; }

  String company = InputTextAction::popup("Company", "");
  if (InputTextAction::wasCancelled()) { render(); return; }

  String address = InputTextAction::popup("Address", "");
  if (InputTextAction::wasCancelled()) { render(); return; }

  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled()) { render(); return; }

  String mail = InputTextAction::popup("Mail", "");
  if (InputTextAction::wasCancelled()) { render(); return; }

  String website = InputTextAction::popup("Website", "https://");
  if (InputTextAction::wasCancelled()) { render(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildVcard(contact, company, address, phone, mail, website,
                               ndef, ndefLen, MAX_NDEF_BYTES)) {
    ShowStatusAction::show("vCard too large");
    render();
    return;
  }

  String name = InputTextAction::popup("File name", "vcard");
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}
