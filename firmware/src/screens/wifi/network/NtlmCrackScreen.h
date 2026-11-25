#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>   // must precede queue.h / semphr.h on ESP32
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "utils/network/NtlmCrack.h"

// ── NetNTLMv2 offline cracker ──────────────────────────────────────────────────
// Dictionary-cracks the NTLMv2 hashes Responder captured to
// /unigeek/wifi/responder/ntlm_hashes.txt (hashcat -m 5600). Reached from the
// Responder screen's Options ("Crack Hashes"), never while capturing — capture
// is a live poll loop, cracking pins both cores, so they are separate phases.
//
// Same dual-core producer/worker design as WifiEapolBruteForceScreen: core 1
// reads the wordlist and feeds a queue while also cracking, core 0 drains the
// queue. Only the per-password test differs (fast_netntlmv2_try_password).
class NtlmCrackScreen : public ListScreen {
public:
  const char* title() override { return "NTLM Crack"; }
  bool inhibitPowerOff() override { return _state == STATE_CRACKING; }

  ~NtlmCrackScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  // ── Dual-core crack ─────────────────────────────────────────────────────
  static constexpr int QUEUE_DEPTH = 8;
  static constexpr int PASS_MAX    = 64;

  struct PwEntry {
    char    pw[PASS_MAX];
    uint8_t len;
  };

  struct CrackCtx {
    NtlmHash          hash;                 // selected capture, copied in
    NtlmTarget        target;               // prepared from &hash
    char              wordlistPath[64] = {};
    QueueHandle_t     queue        = nullptr;
    SemaphoreHandle_t doneSem      = nullptr;
    TaskHandle_t      workerHandle = nullptr;
    volatile bool     stop      = false;
    volatile bool     done      = false;
    volatile bool     found     = false;
    char              foundPass[64] = {};
    char              curPass[64]   = {};
    volatile uint32_t tested     = 0;
    volatile uint32_t bytesDone  = 0;
    volatile uint32_t fileSize   = 0;
    volatile float    speed      = 0.0f;
  };

  static CrackCtx     _ctx;
  static TaskHandle_t _taskHandle;
  static void _crackTask(void* param);    // producer + cracker on core 1
  static void _workerTask(void* param);   // worker cracker on core 0

  // ── State machine ──────────────────────────────────────────────────────────
  enum State { STATE_MENU, STATE_SELECT_HASH, STATE_SELECT_WORDLIST, STATE_CRACKING, STATE_DONE };
  State _state = STATE_MENU;

  // ── Captured hashes (parsed from the Responder file, deduped by user\domain)
  static constexpr uint8_t MAX_HASHES = 32;
  static const char* HASH_FILE;
  static const char* PASS_DIR;

  NtlmHash _hashes[MAX_HASHES];
  String   _hashLabel[MAX_HASHES];   // "user" (list label + backing store)
  String   _hashDomain[MAX_HASHES];  // "domain" (list sublabel)
  ListItem _hashItems[MAX_HASHES] = {};
  uint8_t  _hashCount     = 0;
  int8_t   _selectedHash  = -1;

  // ── Wordlist picker (BrowseFileView + virtual "Built In" tail entry) ────────
  BrowseFileView _browser;
  ListItem _combinedItems[BrowseFileView::kCap + 1] = {};
  uint8_t  _combinedCount = 0;
  bool     _hasBuiltIn    = false;
  String   _currentDir;
  char     _selectedWordlist[64] = {};

  // ── Main menu ──────────────────────────────────────────────────────────────
  ListItem _menuItems[3] = {};
  String   _hashSub;
  String   _wordlistSub;

  bool     _chromeDrawn = false;

  // ── Helpers ────────────────────────────────────────────────────────────────
  void _showMenu();
  uint8_t _loadHashes();          // parse + dedup the Responder file → _hashes
  void _showHashPicker();
  bool _reloadPicker();           // wordlist browser (mirrors EAPOL)
  void _startCrack();
  void _stopCrack();
  void _saveCrackedPassword();
  void _renderCracking();
  void _renderDone();
};
