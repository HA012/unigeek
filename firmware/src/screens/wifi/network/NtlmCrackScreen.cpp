#include "NtlmCrackScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/NtlmCrack.h"

#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>

// ── Built-in wordlist: common Windows/AD credentials (short + long) ─────────
// NTLM passwords have no length floor (unlike WPA2), so this list mixes short
// and long entries. The user's own wordlist is the primary path.
static const char* const kBuiltIn[] = {
  "password",    "Password1",   "Password123", "Passw0rd",    "P@ssw0rd",
  "P@ssw0rd1",   "admin",       "Admin123",    "administrator","welcome",
  "Welcome1",    "Welcome123",  "123456",      "1234567",     "12345678",
  "123456789",   "letmein",     "changeme",    "qwerty",      "qwerty123",
  "abc123",      "monkey",      "dragon",      "football",    "iloveyou",
  "sunshine",    "princess",    "login",       "root",        "toor",
  "guest",       "test",        "test123",     "secret",      "master",
  "superman",    "company123",  "Summer2023",  "Winter2023",  "Spring2024",
  "Autumn2023",  "Password2024","Passw0rd!",   "Admin@123",   "Pa$$w0rd",
  "trustno1",    "baseball",    "hunter2",
};
static constexpr int kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);
static char _builtInSublabel[24] = {};

// ── Statics ─────────────────────────────────────────────────────────────────
const char* NtlmCrackScreen::HASH_FILE = "/unigeek/wifi/responder/ntlm_hashes.txt";
const char* NtlmCrackScreen::PASS_DIR  = "/unigeek/utility/passwords";

NtlmCrackScreen::CrackCtx NtlmCrackScreen::_ctx        = {};
TaskHandle_t              NtlmCrackScreen::_taskHandle = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────
static String _truncSub(const char* name) {
  int len = strlen(name);
  if (len <= 14) return String(name);
  return String("..") + String(name + len - 12);
}

static bool ciEqual(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return false;
    ++a; ++b;
  }
  return *a == *b;
}

NtlmCrackScreen::~NtlmCrackScreen() {
  _stopCrack();
}

// ── Load + dedup captured hashes ────────────────────────────────────────────
uint8_t NtlmCrackScreen::_loadHashes() {
  _hashCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return 0;
  fs::File f = Uni.Storage->open(HASH_FILE, FILE_READ);
  if (!f) return 0;

  char line[1200];   // user + domain + challenge + ntproof + blob(hex) fits
  while (f.available() && _hashCount < MAX_HASHES) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    NtlmHash h;
    if (!ntlm_parse_line(line, h)) continue;

    // Dedup by user\domain (case-insensitive), keeping the latest capture.
    int dup = -1;
    for (uint8_t i = 0; i < _hashCount; ++i) {
      if (ciEqual(_hashes[i].user, h.user) && ciEqual(_hashes[i].domain, h.domain)) {
        dup = i; break;
      }
    }
    uint8_t slot = (dup >= 0) ? (uint8_t)dup : _hashCount;
    _hashes[slot]     = h;
    _hashLabel[slot]  = h.userLen   ? String(h.user)   : String("(null)");
    _hashDomain[slot] = h.domainLen ? String(h.domain) : String("-");
    if (dup < 0) _hashCount++;
  }
  f.close();
  return _hashCount;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
void NtlmCrackScreen::_showMenu() {
  _state = STATE_MENU;

  if (_selectedHash >= 0 && _selectedHash < (int8_t)_hashCount) {
    String dom = _hashDomain[_selectedHash];
    String id  = (dom.length() && dom != "-") ? dom + "\\" + _hashLabel[_selectedHash]
                                              : _hashLabel[_selectedHash];
    _hashSub = _truncSub(id.c_str());
  } else {
    _hashSub = "(not selected)";
  }

  const char* wlBase = strrchr(_selectedWordlist, '/');
  const char* wlName = wlBase ? wlBase + 1 : _selectedWordlist;
  _wordlistSub = _selectedWordlist[0] ? _truncSub(wlName) : "(not selected)";

  _menuItems[0] = {"Hash",     _hashSub.c_str()};
  _menuItems[1] = {"Wordlist", _wordlistSub.c_str()};
  _menuItems[2] = {"Start",    nullptr};
  setItems(_menuItems, 3);
}

void NtlmCrackScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available");
    Screen.goBack();
    return;
  }

  if (_builtInSublabel[0] == '\0')
    snprintf(_builtInSublabel, sizeof(_builtInSublabel), "%d entries", kBuiltInCount);

  Uni.Storage->makeDir(PASS_DIR);
  _selectedWordlist[0] = '\0';
  _selectedHash        = -1;

  if (_loadHashes() == 0) {
    ShowStatusAction::show("No hashes captured");
    Screen.goBack();
    return;
  }
  if (_hashCount == 1) _selectedHash = 0;   // auto-select the only capture
  _showMenu();
}

void NtlmCrackScreen::onUpdate() {
  if (_state == STATE_CRACKING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stopCrack();
        return;
      }
    }
    if (_ctx.done) {
      _state = STATE_DONE;
      _taskHandle = nullptr;
      if (_ctx.queue)   { vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr; }
      if (_ctx.doneSem) { vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr; }
      _ctx.workerHandle = nullptr;
      if (_ctx.found) {
        _saveCrackedPassword();
        if (Uni.Speaker) Uni.Speaker->playWin();
      } else {
        if (Uni.Speaker) Uni.Speaker->playLose();
      }
      render();
    } else {
      static uint32_t lastRender = 0;
      uint32_t now = millis();
      if (now - lastRender >= 1000) { lastRender = now; render(); }
    }
    return;
  }

  if (_state == STATE_DONE) {
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      _showMenu();
    }
    return;
  }

  ListScreen::onUpdate();
}

void NtlmCrackScreen::onRender() {
  if (_state == STATE_CRACKING) { _renderCracking(); return; }
  if (_state == STATE_DONE)     { _renderDone();     return; }
  ListScreen::onRender();
}

void NtlmCrackScreen::onBack() {
  if (_state == STATE_SELECT_HASH) {
    _showMenu();
    return;
  }
  if (_state == STATE_SELECT_WORDLIST) {
    if (_currentDir == PASS_DIR || _currentDir.length() == 0) {
      _showMenu();
      return;
    }
    int slash = _currentDir.lastIndexOf('/');
    _currentDir = (slash > 0) ? _currentDir.substring(0, slash) : String(PASS_DIR);
    _reloadPicker();
    return;
  }
  Screen.goBack();
}

void NtlmCrackScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) {
      _showHashPicker();
    } else if (index == 1) {
      _state = STATE_SELECT_WORDLIST;
      _currentDir = PASS_DIR;
      _reloadPicker();
    } else if (index == 2) {
      if (_selectedHash < 0) {
        ShowStatusAction::show("Hash not selected");
        render();
        return;
      }
      if (_selectedWordlist[0] == '\0') {
        ShowStatusAction::show("Wordlist not selected");
        render();
        return;
      }
      _startCrack();
    }
    return;
  }

  if (_state == STATE_SELECT_HASH) {
    if (index < _hashCount) {
      _selectedHash = (int8_t)index;
      _showMenu();
    }
    return;
  }

  if (_state == STATE_SELECT_WORDLIST) {
    if (index >= _combinedCount) return;
    const bool isBuiltIn = _hasBuiltIn && index == _browser.count();
    if (isBuiltIn) {
      strncpy(_selectedWordlist, "builtin", sizeof(_selectedWordlist) - 1);
      _selectedWordlist[sizeof(_selectedWordlist) - 1] = '\0';
      _showMenu();
      return;
    }
    const auto& e = _browser.entry(index);
    if (e.isDir) {
      _currentDir = e.path;
      _reloadPicker();
    } else {
      strncpy(_selectedWordlist, e.path.c_str(), sizeof(_selectedWordlist) - 1);
      _selectedWordlist[sizeof(_selectedWordlist) - 1] = '\0';
      _showMenu();
    }
    return;
  }
}

// ── Pickers ─────────────────────────────────────────────────────────────────
void NtlmCrackScreen::_showHashPicker() {
  _state = STATE_SELECT_HASH;
  for (uint8_t i = 0; i < _hashCount; ++i)
    _hashItems[i] = { _hashLabel[i].c_str(), _hashDomain[i].c_str() };
  setItems(_hashItems, _hashCount);
}

bool NtlmCrackScreen::_reloadPicker() {
  _browser.root = String(PASS_DIR);
  uint8_t n = _browser.load(this, _currentDir, BrowseFileView::Mode{}, nullptr);

  _combinedCount = 0;
  for (uint8_t i = 0; i < n; i++) _combinedItems[_combinedCount++] = _browser.items()[i];

  _hasBuiltIn = false;
  if (_currentDir == PASS_DIR && _combinedCount < (uint8_t)(BrowseFileView::kCap + 1)) {
    _combinedItems[_combinedCount++] = { "Built In", _builtInSublabel };
    _hasBuiltIn = true;
  }

  setItems(_combinedItems, _combinedCount);
  return _combinedCount > 0;
}

// ── Worker (core 0) ─────────────────────────────────────────────────────────
void NtlmCrackScreen::_workerTask(void* param) {
  CrackCtx* ctx = static_cast<CrackCtx*>(param);
  PwEntry entry;

  while (true) {
    if (xQueueReceive(ctx->queue, &entry, portMAX_DELAY) != pdTRUE) break;
    if (entry.len == 0) break;  // poison pill
    if (ctx->found || ctx->stop) {
      __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
      continue;
    }
    if (fast_netntlmv2_try_password(entry.pw, entry.len, ctx->target)) {
      ctx->found = true;
      memcpy(ctx->foundPass, entry.pw, entry.len + 1);
    }
    __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
  }
  xSemaphoreGive(ctx->doneSem);
  vTaskDelete(NULL);
}

// ── Producer + cracker (core 1) ─────────────────────────────────────────────
void NtlmCrackScreen::_crackTask(void* param) {
  CrackCtx* ctx = static_cast<CrackCtx*>(param);
  uint32_t t0 = millis();

  auto tryHere = [&](const char* pw, size_t len) {
    memcpy(ctx->curPass, pw, len + 1);
    if (fast_netntlmv2_try_password(pw, (uint8_t)len, ctx->target)) {
      memcpy(ctx->foundPass, pw, len + 1);
      ctx->found = true;
    }
    __atomic_fetch_add(&ctx->tested, 1, __ATOMIC_RELAXED);
    uint32_t el = millis() - t0;
    if (el >= 2000) ctx->speed = ctx->tested * 1000.0f / el;
  };

  auto sendToWorker = [&](const char* pw, size_t len) -> bool {
    PwEntry entry;
    memcpy(entry.pw, pw, len + 1);
    entry.len = (uint8_t)len;
    return xQueueSend(ctx->queue, &entry, 0) == pdTRUE;
  };

  if (strcmp(ctx->wordlistPath, "builtin") == 0) {
    int i = 0;
    while (i < kBuiltInCount && !ctx->stop && !ctx->found) {
      size_t n = strlen(kBuiltIn[i]);
      if (n < 1 || n > 63) { i++; ctx->bytesDone = (uint32_t)i; continue; }
      if (!sendToWorker(kBuiltIn[i], n)) {
        tryHere(kBuiltIn[i], n);
        i++;
      } else {
        i++;
        if (i < kBuiltInCount && !ctx->stop && !ctx->found) {
          size_t n2 = strlen(kBuiltIn[i]);
          if (n2 >= 1 && n2 <= 63) tryHere(kBuiltIn[i], n2);
          i++;
        }
      }
      ctx->bytesDone = (uint32_t)i;
    }
  } else {
    char line[64];
    char line2[64];
    fs::File f = Uni.Storage->open(ctx->wordlistPath, FILE_READ);
    if (f) {
      while (f.available() && !ctx->stop && !ctx->found) {
        size_t n = f.readBytesUntil('\n', line, 63);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = '\0';
        if (n < 1 || n > 63) { ctx->bytesDone = f.position(); continue; }

        if (!sendToWorker(line, n)) {
          tryHere(line, n);
        } else if (f.available() && !ctx->stop && !ctx->found) {
          size_t n2 = f.readBytesUntil('\n', line2, 63);
          line2[n2] = '\0';
          while (n2 > 0 && (line2[n2 - 1] == '\r' || line2[n2 - 1] == '\n')) line2[--n2] = '\0';
          if (n2 >= 1 && n2 <= 63) tryHere(line2, n2);
        }
        ctx->bytesDone = f.position();
      }
      f.close();
    }
  }

  PwEntry poison;
  memset(&poison, 0, sizeof(poison));
  xQueueSend(ctx->queue, &poison, pdMS_TO_TICKS(2000));
  xSemaphoreTake(ctx->doneSem, pdMS_TO_TICKS(5000));

  ctx->done = true;
  vTaskDelete(nullptr);
}

// ── Start / Stop ──────────────────────────────────────────────────────────
void NtlmCrackScreen::_startCrack() {
  memset(_ctx.foundPass, 0, sizeof(_ctx.foundPass));
  memset(_ctx.curPass,   0, sizeof(_ctx.curPass));
  _ctx.stop      = false;
  _ctx.done      = false;
  _ctx.found     = false;
  _ctx.tested    = 0;
  _ctx.bytesDone = 0;
  _ctx.speed     = 0.0f;

  // Copy the selected capture into the static ctx so target.h stays valid for
  // the task lifetime, then precompute the per-hash constants.
  _ctx.hash = _hashes[_selectedHash];
  ntlm_prepare(_ctx.hash, _ctx.target);

  strncpy(_ctx.wordlistPath, _selectedWordlist, sizeof(_ctx.wordlistPath) - 1);
  _ctx.wordlistPath[sizeof(_ctx.wordlistPath) - 1] = '\0';

  if (strcmp(_selectedWordlist, "builtin") == 0) {
    _ctx.fileSize = kBuiltInCount;
  } else {
    fs::File f = Uni.Storage->open(_selectedWordlist, FILE_READ);
    _ctx.fileSize = f ? f.size() : 0;
    if (f) f.close();
  }

  _ctx.queue   = xQueueCreate(QUEUE_DEPTH, sizeof(PwEntry));
  _ctx.doneSem = xSemaphoreCreateBinary();
  if (!_ctx.queue || !_ctx.doneSem) {
    if (_ctx.queue)   { vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr; }
    if (_ctx.doneSem) { vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr; }
    ShowStatusAction::show("Out of memory");
    render();
    return;
  }

  BaseType_t wOk = xTaskCreatePinnedToCore(_workerTask, "ntlm_w", 8192, &_ctx, 1, &_ctx.workerHandle, 0);
  BaseType_t pOk = xTaskCreatePinnedToCore(_crackTask,  "ntlm_p", 8192, &_ctx, 1, &_taskHandle,       1);
  if (wOk != pdPASS || pOk != pdPASS) {
    PwEntry poison; memset(&poison, 0, sizeof(poison));
    xQueueSend(_ctx.queue, &poison, portMAX_DELAY);
    if (wOk == pdPASS) { xSemaphoreTake(_ctx.doneSem, pdMS_TO_TICKS(1000)); vTaskDelete(_ctx.workerHandle); _ctx.workerHandle = nullptr; }
    if (pOk == pdPASS) { vTaskDelete(_taskHandle); _taskHandle = nullptr; }
    vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr;
    vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr;
    ShowStatusAction::show("Failed");
    render();
    return;
  }

  _state       = STATE_CRACKING;
  _chromeDrawn = false;
  render();
}

void NtlmCrackScreen::_stopCrack() {
  if (!_taskHandle) return;
  _ctx.stop = true;
  for (int i = 0; i < 200 && !_ctx.done; i++) vTaskDelay(10 / portTICK_PERIOD_MS);
  if (!_ctx.done) {
    vTaskDelete(_taskHandle);
    if (_ctx.workerHandle) vTaskDelete(_ctx.workerHandle);
  }
  _taskHandle = nullptr;
  _ctx.workerHandle = nullptr;
  if (_ctx.queue)   { vQueueDelete(_ctx.queue);       _ctx.queue   = nullptr; }
  if (_ctx.doneSem) { vSemaphoreDelete(_ctx.doneSem); _ctx.doneSem = nullptr; }
  if (_state == STATE_CRACKING) {
    _state = STATE_DONE;
    render();
  }
}

// ── Save cracked credential ─────────────────────────────────────────────────
void NtlmCrackScreen::_saveCrackedPassword() {
  if (!Uni.Storage || !_ctx.found) return;
  static const char* dir = "/unigeek/wifi/responder";
  Uni.Storage->makeDir(dir);
  String path = String(dir) + "/cracked.txt";
  fs::File f = Uni.Storage->open(path.c_str(), FILE_APPEND);
  if (f) {
    f.printf("%s\\%s:%s\n", _ctx.hash.domain, _ctx.hash.user, _ctx.foundPass);
    f.close();
  }
}

// ── Render ────────────────────────────────────────────────────────────────
void NtlmCrackScreen::_renderCracking() {
  auto& lcd = Uni.Lcd;
  const uint16_t accent = Config.getThemeColor();
  const int      lh     = 14;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

    char who[40];
    snprintf(who, sizeof(who), "%s\\%s", _ctx.hash.domain, _ctx.hash.user);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("User:", bodyX() + 4, bodyY() + 4);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(who, bodyX() + 40, bodyY() + 4);

#ifndef DEVICE_HAS_KEYBOARD
    static constexpr int FOOTER_H = 12;
    const int footerY = bodyY() + bodyH() - FOOTER_H;
    lcd.drawFastHLine(bodyX(), footerY - 1, bodyW(), TFT_DARKGREY);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("[Any button] Stop", bodyX() + bodyW() / 2, footerY + 7);
#endif
    _chromeDrawn = true;
  }

  const int barPad = 3;
  const int barH   = 8 + barPad * 2;
  const int dynH   = lh + 4 + barH + 6 + lh + 2;
  const int dynY   = bodyY() + 4 + lh;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), dynH);
  sp.fillSprite(TFT_BLACK);

  int y = 0;

  char disp[24];
  snprintf(disp, sizeof(disp), "%.23s", _ctx.curPass);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Try:", 4, y);
  sp.setTextColor(accent, TFT_BLACK);
  sp.drawString(disp, 36, y);
  y += lh + 4;

  const int barX = barPad;
  const int barW = bodyW() - barPad * 2;
  int pct = (_ctx.fileSize > 0) ? (int)((uint64_t)_ctx.bytesDone * 100 / _ctx.fileSize) : 0;
  if (pct > 100) pct = 100;

  sp.drawRect(barX, y, barW, barH, TFT_DARKGREY);
  if (pct > 0) sp.fillRect(barX + 1, y + 1, (barW - 2) * pct / 100, barH - 2, accent);

  char pctBuf[6];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, barX + barW / 2, y + barH / 2);
  y += barH + 6;

  char statBuf[48];
  snprintf(statBuf, sizeof(statBuf), "%.1f/s  |  %lu tested",
           (float)_ctx.speed, (unsigned long)_ctx.tested);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(statBuf, bodyW() / 2, y);

  sp.pushSprite(bodyX(), dynY);
  sp.deleteSprite();
}

void NtlmCrackScreen::_renderDone() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  const int cx = bodyX() + bodyW() / 2;
  const int cy = bodyY() + bodyH() / 2;

  if (_ctx.found) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.drawString("PASSWORD FOUND", cx, cy - 22);

    int pwW  = lcd.textWidth(_ctx.foundPass);
    int padX = 6, padY = 3;
    int rx = cx - pwW / 2 - padX;
    int ry = cy - 6 - 6 - padY;
    lcd.fillRoundRect(rx, ry, pwW + padX * 2, 12 + padY * 2, 4, Config.getThemeColor());
    lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
    lcd.drawString(_ctx.foundPass, cx, cy - 6);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu tries", (unsigned long)_ctx.tested);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(buf, cx, cy + 10);
  } else {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString("Not in wordlist", cx, cy - 10);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu tested", (unsigned long)_ctx.tested);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(buf, cx, cy + 6);
  }

  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Any key to continue", cx, bodyY() + bodyH());
}
