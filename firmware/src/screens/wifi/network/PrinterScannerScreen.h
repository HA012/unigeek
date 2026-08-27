#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/SsdpScanUtil.h"
#include "utils/network/MdnsScanUtil.h"

class PrinterScannerScreen : public ListScreen
{
public:
  const char* title() override { return "Printer Scanner"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_IDLE,
    STATE_SCANNING,
    STATE_RESULTS,
    STATE_DETAILS,
  };

  struct Printer {
    char name[48];
    char ip[16];
    char host[64];
    char source[16];
    uint16_t port;
    char service[48];
    char server[96];
    char location[200];
  };

  static constexpr uint8_t MAX_PRINTERS = 24;

  State _state = STATE_IDLE;

  Printer _printers[MAX_PRINTERS];
  uint8_t _printerCount = 0;

  ListItem _idleItems[1] = {
    {"Start Scan"},
  };

  ListItem _resultItems[MAX_PRINTERS];
  String   _resultSubs[MAX_PRINTERS];

  static constexpr uint8_t DETAIL_ROWS = 8;
  ListItem _detailItems[DETAIL_ROWS];
  String   _detailSubs[DETAIL_ROWS];

  void _showIdle();
  void _scan();
  void _showResults();
  void _showDetails(uint8_t index);

  Printer* _findByIp(const char* ip);
  Printer* _findByHost(const char* host);
  Printer* _addPrinter();
  void _mergeSsdp(const SsdpScanUtil::Device& dev);
  void _mergeMdns(const MdnsScanUtil::Service& svc);
};
