#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/StatusBar.h"
#include "ui/components/Header.h"

class DictionaryKeyProgressView {
public:
  static void begin(const char* dictionary, size_t keyTotal, uint8_t recoverStage = 0) {
    auto& lcd = Uni.Lcd;
    const int bx = StatusBar::WIDTH;
    const int by = Header::HEIGHT;
    const int bw = lcd.width() - StatusBar::WIDTH - 4;
    const int bh = lcd.height() - by - 4;
    const int pad = 8;

    lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    char dictionaryLine[96];
    if (recoverStage)
      snprintf(dictionaryLine, sizeof(dictionaryLine), "%s (%u/3)", dictionary ? dictionary : "", (unsigned)recoverStage);
    else
      snprintf(dictionaryLine, sizeof(dictionaryLine), "%s", dictionary ? dictionary : "");
    lcd.setTextDatum(TL_DATUM);
    lcd.drawString(dictionaryLine, bx + pad, by + 7);
    drawKeyCounter(lcd, bx, by, bw, pad, 0, keyTotal);

    const int barH = 10;
    const int barX = bx + pad;
    const int barW = bw - pad * 2;
    const int bar1Y = by + bh - 44;
    const int bar2Y = by + bh - 20;
    const uint16_t theme = Config.getThemeColor();
    initBar(lcd, barX, bar1Y, barW, barH, theme);
    initBar(lcd, barX, bar2Y, barW, barH, theme);
  }

  static void draw(const char* dictionary, uint8_t sector, char keyType,
                   const uint8_t key[6], size_t keyIndex, size_t keyTotal,
                   size_t slotIndex, size_t slotTotal, bool found = false,
                   bool hideCounter = false) {
    auto& lcd = Uni.Lcd;
    const int bx = StatusBar::WIDTH;
    const int by = Header::HEIGHT;
    const int bw = lcd.width() - StatusBar::WIDTH - 4;
    const int bh = lcd.height() - by - 4;
    const int pad = 8;
    const int barH = 10;
    const int barX = bx + pad;
    const int barW = bw - pad * 2;
    const int bar1Y = by + bh - 44;
    const int bar2Y = by + bh - 20;
    const int keyLineY = bar1Y - 14;
    const int sectorLineY = keyLineY - 20;
    const uint16_t theme = Config.getThemeColor();
    const uint16_t stateColor = found ? TFT_GREEN : TFT_RED;
    drawKeyCounter(lcd, bx, by, bw, pad, keyIndex, keyTotal);

    // Sector is a stable visual anchor: keep it white, centered and slightly
    // larger than the rapidly changing key-attempt row below it.
    char sectorText[8];
    snprintf(sectorText, sizeof(sectorText), "S%02u", (unsigned)sector);
    lcd.fillRect(bx + pad, sectorLineY, barW, 18, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(sectorText, bx + bw / 2, sectorLineY);

    char typeText[4];
    char keyText[13];
    snprintf(typeText, sizeof(typeText), "%c", keyType);
    snprintf(keyText, sizeof(keyText), "%02X%02X%02X%02X%02X%02X",
             key[0], key[1], key[2], key[3], key[4], key[5]);

    // Center A/B + key + counter as one composition. Only A/B and the key
    // carry state color; the counter remains neutral.
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.fillRect(bx + pad, keyLineY, barW, 10, TFT_BLACK);
    const int gap = 5;
    const int totalW = lcd.textWidth(typeText) + gap + lcd.textWidth(keyText);
    int x = bx + (bw - totalW) / 2;

    lcd.setTextColor(stateColor, TFT_BLACK);
    lcd.drawString(typeText, x, keyLineY);
    x += lcd.textWidth(typeText) + gap;
    lcd.drawString(keyText, x, keyLineY);

    // The key bar restarts for every A/B slot; the slot bar tracks the
    // physical Sx A/B position and therefore advances independently of hits.
    if (keyIndex <= 1U) clearBar(lcd, barX, bar1Y, barW, barH);
    fillBar(lcd, barX, bar1Y, barW, barH,
            keyTotal ? percent(keyIndex, keyTotal) : 0, theme);
    fillBar(lcd, barX, bar2Y, barW, barH,
            slotTotal ? percent(slotIndex, slotTotal) : 0, theme);
  }

private:
  template <typename Display>
  static void drawKeyCounter(Display& lcd, int bx, int by, int bw, int pad,
                             size_t keyIndex, size_t keyTotal) {
    char text[32];
    snprintf(text, sizeof(text), "%u/%u keys", (unsigned)keyIndex, (unsigned)keyTotal);
    lcd.setTextSize(1);
    lcd.setTextDatum(TR_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.fillRect(bx + bw / 2, by + 7, bw / 2 - pad, 10, TFT_BLACK);
    lcd.drawString(text, bx + bw - pad, by + 7);
  }

  static uint8_t percent(size_t value, size_t total) {
    if (!total) return 0;
    const size_t pct = value * 100U / total;
    return (uint8_t)(pct > 100U ? 100U : pct);
  }

  template <typename Display>
  static void initBar(Display& lcd, int x, int y, int w, int h, uint16_t color) {
    lcd.drawRect(x, y, w, h, color);
    lcd.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
  }

  template <typename Display>
  static void clearBar(Display& lcd, int x, int y, int w, int h) {
    lcd.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
  }

  template <typename Display>
  static void fillBar(Display& lcd, int x, int y, int w, int h, uint8_t pct, uint16_t color) {
    const int fill = (w - 2) * pct / 100;
    if (fill > 0) lcd.fillRect(x + 1, y + 1, fill, h - 2, color);
  }
};
