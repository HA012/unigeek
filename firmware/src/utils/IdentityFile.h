#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "utils/rfid/LFCodec.h"

namespace IdentityFile {

bool saveNfcUid(const String& path, const uint8_t* uid, size_t uidLen);
bool saveLfId(const String& path, LFCodec::Protocol protocol,
              const uint8_t* data, size_t dataLen);

}  // namespace IdentityFile
