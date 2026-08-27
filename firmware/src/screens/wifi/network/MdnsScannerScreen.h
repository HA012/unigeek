#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/MdnsScanUtil.h"

class MdnsScannerScreen : public ListScreen
{
public:
  const char* title() override { return "mDNS Scanner"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,
    STATE_SCANNING,
    STATE_RESULTS,
    STATE_DETAILS,
  };

  State _state = STATE_CONFIG;

  static constexpr uint8_t SERVICE_COUNT = 5;
  static const char* SERVICE_LABELS[SERVICE_COUNT];
  static const char* SERVICE_TYPES[SERVICE_COUNT];

  uint8_t _serviceIndex = 0;
  String _serviceSub;

  MdnsScanUtil::Service _results[MdnsScanUtil::MAX_RESULTS];
  uint8_t _resultCount = 0;

  ListItem _configItems[2];
  ListItem _resultItems[MdnsScanUtil::MAX_RESULTS];
  String   _resultSubs[MdnsScanUtil::MAX_RESULTS];

  static constexpr uint8_t DETAIL_ROWS = 6;
  ListItem _detailItems[DETAIL_ROWS];
  String   _detailSubs[DETAIL_ROWS];

  void _showConfig();
  void _scan();
  void _showResults();
  void _showDetails(uint8_t index);
};
