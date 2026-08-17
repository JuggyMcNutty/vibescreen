#ifndef __INPUTSHAPER_PANEL_H__
#define __INPUTSHAPER_PANEL_H__

#include "websocket_client.h"
#include "event_guard.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <mutex>

class InputShaperPanel {
 public:
  InputShaperPanel(KWebSocketClient &c, std::mutex &l);
  ~InputShaperPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_image_clicked(lv_event_t *event);
  void handle_macro_response(json &j);
  void handle_update_slider(lv_event_t *event);
  void handle_klippy_gone();
  
  static void _handle_callback(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_callback", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };

  static void _handle_image_clicked(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_image_clicked", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_image_clicked(event);
    });
  };

  static void _handle_update_slider(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_update_slider", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_update_slider(event);
    });
  };

  // Index of a shaper in the list, or -1 when it is not one we know.
  int32_t find_shaper_index(const std::vector<std::string> &s,
			    const std::string &shaper);

  // Point a dropdown at a shaper by name. Returns false and leaves the
  // selection untouched when the name is not one of ours, so the caller
  // decides what to do rather than having a wrong value picked for it.
  bool select_shaper(lv_obj_t *dd, const std::string &shaper);

  // Enable or disable Calibrate and Save from what the printer's config says it
  // can do, and say why on the status line when either is off.
  void update_available();

  void set_status(const std::string &text);

  // Where an axis has got to. Anything but idle means a spinner is up and the
  // panel is waiting on the printer for something.
  enum class RunState { idle, testing, analysing };

  void start_run(bool is_x);
  void finish_run(bool is_x);

  // Give up on runs in flight and say why. Not per axis: while both axes are
  // queued at once there is no telling which of them a failure belongs to, and
  // a spinner nothing ever clears is the worse outcome. analysing_only spares
  // an axis whose test has not run yet, for the failures that can only have
  // come from the analysis step.
  void abandon_runs(const std::string &why, bool analysing_only = false);
  void check_timeouts();

  static void _handle_watchdog(lv_timer_t *timer) {
    KGuard::event("InputShaperPanel::_handle_watchdog", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)timer->user_data;
      panel->check_timeouts();
    });
  };

  void set_shaper_detail(json &res,
			 lv_obj_t *label,
			 lv_obj_t *slider,
			 lv_obj_t *slider_label,
			 lv_obj_t *dd);
  
 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;

  // xgraph
  lv_obj_t *xgraph_cont;
  lv_obj_t *xgraph;
  lv_obj_t *xoutput; // calibrate shaper output x
  lv_obj_t *xspinner;

  // y graph
  lv_obj_t *ygraph_cont;
  lv_obj_t *ygraph;
  lv_obj_t *youtput; // calibrate shaper output y
  lv_obj_t *yspinner;

  // x controls
  lv_obj_t *xcontrol;
  lv_obj_t *xaxis_label;
  lv_obj_t *x_switch;
  lv_obj_t *xslider_cont;
  lv_obj_t *xslider;
  lv_obj_t *xlabel;
  lv_obj_t *xshaper_dd;

  // y controls
  lv_obj_t *ycontrol;
  lv_obj_t *yaxis_label;
  lv_obj_t *y_switch;
  lv_obj_t *yslider_cont;
  lv_obj_t *yslider;
  lv_obj_t *ylabel;
  lv_obj_t *yshaper_dd;

  // Why a button is off, or what a run is doing. Floats over the top of the
  // graph area, which is empty whenever there is anything to say here.
  lv_obj_t *status;

  lv_obj_t *button_cont;
  lv_obj_t *switch_cont;
  lv_obj_t *graph_switch_label;
  lv_obj_t *graph_switch;
  ButtonContainer calibrate_btn;
  ButtonContainer save_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;
  bool ximage_fullsized;
  bool yimage_fullsized;
  json calibrate_output;

  // Whether the shaper type showing for each axis came from a name we
  // recognised. Save is disabled while either is false, because what it would
  // write is whatever the dropdown happens to be sitting on rather than
  // anything the printer told us.
  bool xshaper_known;
  bool yshaper_known;

  RunState xrun;
  RunState yrun;
  // Whether the analysis currently bracketed by gcode_shell_command's own
  // "Running Command" and "finished" lines has printed a result yet.
  bool analysis_produced_result;
  // lv_tick when each axis last changed state, for the watchdog.
  uint32_t xrun_since;
  uint32_t yrun_since;
  lv_timer_t *watchdog;

  static std::vector<std::string> shapers;
  
};

#endif // __INPUTSHAPER_PANEL_H__
