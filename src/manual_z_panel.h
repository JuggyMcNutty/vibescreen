#ifndef __MANUAL_Z_PANEL_H__
#define __MANUAL_Z_PANEL_H__

#include "button_container.h"
#include "selector.h"
#include "notify_consumer.h"
#include "websocket_client.h"
#include "event_guard.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <string>

// Homing Z by hand, for a printer whose Z probe has failed. On a K1 that is
// the load cell in the bed, which both G28 Z and START_PRINT home against, so
// a dead one leaves the machine unable to print at all. Issue #7.
//
// X and Y home as usual. Z is then declared to be at the bottom of its travel,
// raised until a sheet of paper drags under the nozzle, and declared zero
// there. Creality's PRINT_PREPARED makes the next START_PRINT keep that zero
// instead of homing Z again.
class ManualZPanel : public NotifyConsumer {
 public:
  ManualZPanel(KWebSocketClient &ws, std::mutex &lock);
  ~ManualZPanel();

  void foreground();
  void consume(json &j);
  void handle_callback(lv_event_t *event);
  void handle_selector(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    KGuard::event("ManualZPanel::_handle_callback", [&] {
      ManualZPanel *panel = (ManualZPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };

  static void _handle_selector(lv_event_t *event) {
    KGuard::event("ManualZPanel::_handle_selector", [&] {
      ManualZPanel *panel = (ManualZPanel*)event->user_data;
      panel->handle_selector(event);
    });
  };

 private:
  void start();
  void set_zero();
  void jog(double distance);
  void refresh();

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *info_cont;
  lv_obj_t *steps_label;
  lv_obj_t *position_label;
  lv_obj_t *offset_label;
  lv_obj_t *next_print_label;
  ButtonContainer home_xy_btn;
  ButtonContainer start_btn;
  ButtonContainer zup_btn;
  ButtonContainer zdown_btn;
  ButtonContainer zero_btn;
  ButtonContainer prepare_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;
  Selector step_selector;

  // Set Z=0 has been sent since Z was last declared or lost. Until then the
  // position is the bottom of travel or whatever a jog made of it, and handing
  // that to a print would drive the bed into the nozzle.
  bool zeroed;
  // A refused jog, kept on screen until the printer's state next changes.
  std::string jog_refusal;
};

#endif // __MANUAL_Z_PANEL_H__
