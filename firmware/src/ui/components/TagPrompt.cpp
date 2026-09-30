#include "ui/components/TagPrompt.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"
#include "core/Device.h"

void TagPrompt::show(const char* message, int16_t x, int16_t y, int16_t w, int16_t h,
                     const char* title)
{
  Header header;
  if (title) header.render(title);
  StatusBar::refresh();

  auto& lcd = Uni.Lcd;
  lcd.fillRect(x, y, w, h, TFT_BLACK);

  if (message) {
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.drawString(message, x + w / 2, y + h / 2);
    lcd.setTextDatum(TL_DATUM);
  }
}
