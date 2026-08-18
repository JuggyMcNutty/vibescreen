#ifndef __BELTS_CALIBRATION_PANEL_H__
#define __BELTS_CALIBRATION_PANEL_H__

#include "websocket_client.h"
#include "event_guard.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <mutex>

class BeltsCalibrationPanel {
 public:
  BeltsCalibrationPanel(KWebSocketClient &c, std::mutex &l);
  ~BeltsCalibrationPanel();

  void foreground();
  void update_available();
  void handle_callback(lv_event_t *event);
  void handle_image_clicked(lv_event_t *event);
  void handle_macro_response(json &j);
  void handle_update_slider(lv_event_t *event);
  void handle_klippy_gone();
  void check_timeouts();

  static void _handle_callback(lv_event_t *event) {
    KGuard::event("BeltsCalibrationPanel::_handle_callback", [&] {
      BeltsCalibrationPanel *panel = (BeltsCalibrationPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };
  
  static void _handle_image_clicked(lv_event_t *event) {
    KGuard::event("BeltsCalibrationPanel::_handle_image_clicked", [&] {
      BeltsCalibrationPanel *panel = (BeltsCalibrationPanel*)event->user_data;
      panel->handle_image_clicked(event);
    });
  };

  static void _handle_update_slider(lv_event_t *event) {
    KGuard::event("BeltsCalibrationPanel::_handle_update_slider", [&] {
      BeltsCalibrationPanel *panel = (BeltsCalibrationPanel*)event->user_data;
      panel->handle_update_slider(event);
    });
  };

  static void _handle_watchdog(lv_timer_t *timer) {
    KGuard::event("BeltsCalibrationPanel::_handle_watchdog", [&] {
      BeltsCalibrationPanel *panel = (BeltsCalibrationPanel*)timer->user_data;
      panel->check_timeouts();
    });
  };

 private:
  // Both belts used to go out in one macro with only M400 between them. M400
  // waits for moves to finish, not for the accelerometer writer to flush its
  // CSV, so two full sweeps were held at once on a machine with 118MB free.
  // Klipper says when each file is written, which is what advances this.
  enum class RunState { idle, testing_b, testing_a, analysing };

  void start_test(RunState state, const std::string &axis, const char *name,
		  const std::string &status);
  void request_analysis();
  void finish_run(const std::string &png_path);
  void abandon_run(const std::string &why);
  void set_status(const std::string &text);

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *graph_cont;
  lv_obj_t *graph;
  lv_obj_t *spinner;
  lv_obj_t *excite_control;
  lv_obj_t *excite_slider;
  lv_obj_t *excite_label;
  lv_obj_t *excite_dd;
  lv_obj_t *status_label;
  lv_obj_t *button_cont;
  ButtonContainer calibrate_btn;
  ButtonContainer excite_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;
  bool image_fullsized;

  lv_timer_t *watchdog;
  RunState run;
  uint32_t run_since;   // lv_tick when run last changed, for the watchdog
  bool analysis_produced_result;

  static std::vector<std::string> axes;

};

#endif // __BELTS_CALIBRATION_PANEL_H__
