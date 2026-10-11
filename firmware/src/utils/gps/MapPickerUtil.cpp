#include "utils/gps/MapPickerUtil.h"

#include <WiFi.h>
#include "ui/actions/ShowStatusAction.h"

bool MapPickerUtil::ensureWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected");
    return false;
  }
  return true;
}
