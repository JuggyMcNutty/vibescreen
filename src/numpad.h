#ifndef __NUMPAD_H__
#define __NUMPAD_H__

#include "lvgl/lvgl.h"
#include "event_guard.h"
#include <functional>

class Numpad {
 public:
  // decimal adds a sign key and a decimal point. Without it the pad is digits
  // only, which is all a temperature target needs.
  Numpad(lv_obj_t *parent, bool decimal = false);
  ~Numpad();

  void set_callback(std::function<void(double)> cb);
  // Values outside [min, max] are refused rather than handed to the callback.
  void set_range(double min, double max);
  void handle_input(lv_event_t *event);
  void foreground_reset();

  static void _handle_input(lv_event_t *event) {
    KGuard::event("Numpad::_handle_input", [&] {
      Numpad *panel = (Numpad*)event->user_data;
      panel->handle_input(event);
    });
  };

 private:
  lv_obj_t *edit_cont;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::function<void(double)> ready_cb;
  double min_value;
  double max_value;
};

#endif // __NUMPAD_H__
