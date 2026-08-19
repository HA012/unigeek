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
  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  String suggestedName;

  if (!buildRecordInteractive(index, ndef, ndefLen, sizeof(ndef), suggestedName)) {
    render();
    return;
  }

  String name = InputTextAction::popup("File name", suggestedName.c_str());
  if (!InputTextAction::wasCancelled()) _saveNdef(ndef, ndefLen, name);
  render();
}

bool NdefGeneratorScreen::buildRecordInteractive(uint8_t index,
                                                 uint8_t* out, size_t& outLen,
                                                 size_t maxLen,
                                                 String& suggestedName) {
  if (!out || maxLen == 0) return false;
  outLen = 0;

  switch (index) {
    case 0: {
      String value = InputTextAction::popup("Text", "");
      if (InputTextAction::wasCancelled() || value.length() == 0) return false;
      if (!NdefBuilder::buildText(value, out, outLen, maxLen)) {
        ShowStatusAction::show("Text too long");
        return false;
      }
      suggestedName = "text";
      return true;
    }

    case 1: {
      String url = InputTextAction::popup("URL", "https://");
      if (InputTextAction::wasCancelled() || url.length() == 0) return false;
      if (!NdefBuilder::buildUrl(url, out, outLen, maxLen)) {
        ShowStatusAction::show("URL too long");
        return false;
      }
      suggestedName = "url";
      return true;
    }

    case 2: {
      String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled() || phone.length() == 0) return false;
      if (!NdefBuilder::buildPhone(phone, out, outLen, maxLen)) {
        ShowStatusAction::show("Phone too long");
        return false;
      }
      suggestedName = "phone";
      return true;
    }

    case 3: {
      String email = InputTextAction::popup("Email", "");
      if (InputTextAction::wasCancelled() || email.length() == 0) return false;
      if (!NdefBuilder::buildEmail(email, out, outLen, maxLen)) {
        ShowStatusAction::show("Email too long");
        return false;
      }
      suggestedName = "email";
      return true;
    }

    case 4: {
      String contact = InputTextAction::popup("Contact name", "");
      if (InputTextAction::wasCancelled() || contact.length() == 0) return false;

      String company = InputTextAction::popup("Company", "");
      if (InputTextAction::wasCancelled()) return false;

      String address = InputTextAction::popup("Address", "");
      if (InputTextAction::wasCancelled()) return false;

      String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled()) return false;

      String mail = InputTextAction::popup("Mail", "");
      if (InputTextAction::wasCancelled()) return false;

      String website = InputTextAction::popup("Website", "https://");
      if (InputTextAction::wasCancelled()) return false;

      if (!NdefBuilder::buildVcard(contact, company, address, phone, mail, website,
                                   out, outLen, maxLen)) {
        ShowStatusAction::show("vCard too large");
        return false;
      }
      suggestedName = "vcard";
      return true;
    }
  }

  return false;
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
