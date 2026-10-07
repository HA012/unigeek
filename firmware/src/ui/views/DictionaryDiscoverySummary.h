#pragma once
#include <Arduino.h>
#include <cstring>
#include "LogView.h"

// Keeps distinct recovered key values in discovery order. The full history is
// retained; render() shows as many entries as fit in the summary view.
class DictionaryDiscoverySummary {
 public:
  void note(const uint8_t key[6], char type) {
    char hex[13];
    snprintf(hex,sizeof(hex),"%02X%02X%02X%02X%02X%02X",
             key[0],key[1],key[2],key[3],key[4],key[5]);
    for (int i=0;i<_count;++i) if (_entries[i].hex==hex) return;
    if (_count < kMaxEntries) _entries[_count++]={String(hex),type};
  }
  template<class Count>
  void render(LogView& log, size_t sectors, Count count, bool canStop, bool complete) const {
    log.clear();
    log.addLine("Keys recovered:",TFT_WHITE);
    const int visible = _count < kMaxVisibleEntries ? _count : kMaxVisibleEntries;
    const int first = _count - visible;
    for(int i=first;i<_count;++i){
      char line[48];
      snprintf(line,sizeof(line),"%c %s",_entries[i].type,_entries[i].hex.c_str());
      log.addLine(line,TFT_GREEN);
      snprintf(line,sizeof(line),"%u/%u sectors authenticated",
               (unsigned)count(_entries[i].hex),(unsigned)sectors);
      log.addLine(line,TFT_WHITE);
    }
    if(complete) {
      log.addLine("All keys recovered",TFT_GREEN);
      log.addLine("[Press] Continue",TFT_YELLOW);
    }
    else {
      log.addLine("[Press] Continue key check",TFT_YELLOW);
      if(canStop) log.addLine("[Back] Stop key check",TFT_YELLOW);
    }
  }
 private:
  static constexpr int kMaxEntries = 32;
  static constexpr int kMaxVisibleEntries = 3;
  struct Entry {String hex;char type;};
  Entry _entries[kMaxEntries];
  int _count=0;
};
