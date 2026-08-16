#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void enable_spoolman();
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  // Apply the printer's own extruder limits to the selectors, so options the
  // printer would reject are greyed out instead of producing a Klipper error.
  // Same shape as LimitsPanel::init.
  void init(json &j);

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  SpoolmanPanel &spoolman_panel;
  SensorContainer extruder_temp;
  Selector temp_selector;
  Selector length_selector;
  Selector speed_selector;
  lv_obj_t *rightside_btns_cont;
  lv_obj_t *leftside_btns_cont;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  ButtonContainer cooldown_btn;
  ButtonContainer spoolman_btn;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;
  ButtonContainer back_btn;
  std::string load_filament_macro;
  std::string unload_filament_macro;
  std::string cooldown_macro;

  // Numeric value behind each selector button, indexed the same as the
  // buttons. Kept so limits can be applied without reparsing the labels.
  std::vector<double> temp_values;
  std::vector<double> length_values;
  std::vector<double> speed_values;

  // Latest readings from the printer, used to skip a redundant M109.
  // -1 means nothing has been reported yet.
  int current_temp;
  int current_target;

  // Klipper's own limits. Defaults are permissive so that a printer which does
  // not report them behaves exactly as it did before any clamping existed.
  double min_extrude_temp;

  void apply_extrude_limits();
  void refresh_extrude_buttons();
  void send_extrude_move(int direction);
};

#endif // __EXTRUDER_PANEL_H__
