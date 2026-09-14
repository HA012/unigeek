#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfcAdvancedScreen : public ListScreen {
public:
  const char* title() override { return _showingMemory ? "Read Memory" : "Advanced"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[4];
  bool _showingMemory = false;
  uint8_t _selMenu = 0;
  ScrollListView _view;
  // Keep the same capacity as PN532: 3 header rows + 2 rows for each
  // of the 256 MIFARE Classic 4K blocks.
  static constexpr size_t MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _labels[MAX_ROWS];
  String _values[MAX_ROWS];
  uint16_t _rowCount = 0;
  void _readMemory();
  void _editMemory();
  void _editUid();
  void _lockUidGen3();
  void _addRow(const String& label, const String& value);
};
