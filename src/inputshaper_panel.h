#ifndef __INPUTSHAPER_PANEL_H__
#define __INPUTSHAPER_PANEL_H__

#include "websocket_client.h"
#include "event_guard.h"
#include "button_container.h"
#include "selector.h"
#include "lvgl/lvgl.h"

#include <array>
#include <string>
#include <vector>
#include <mutex>

// Resonance testing and input shaper tuning, one axis at a time.
//
// The panel shows a single axis: its plot or its numbers, its shaper type and
// its frequency. The axis row picks which one, and picks what Calibrate will
// run. Showing both at once was how this started out, and it bought two
// half-size plots and two of every control on a 480px panel.
class InputShaperPanel {
 public:
  InputShaperPanel(KWebSocketClient &c, std::mutex &l);
  ~InputShaperPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_selector(lv_event_t *event);
  void handle_macro_response(json &j);
  void handle_update_slider(lv_event_t *event);
  void handle_klippy_gone();
  void check_timeouts();

  static void _handle_callback(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_callback", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };

  static void _handle_selector(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_selector", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_selector(event);
    });
  };

  static void _handle_update_slider(lv_event_t *event) {
    KGuard::event("InputShaperPanel::_handle_update_slider", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
      panel->handle_update_slider(event);
    });
  };

  static void _handle_watchdog(lv_timer_t *timer) {
    KGuard::event("InputShaperPanel::_handle_watchdog", [&] {
      InputShaperPanel *panel = (InputShaperPanel*)timer->user_data;
      panel->check_timeouts();
    });
  };

 private:
  // Where an axis has got to. Anything but idle means a spinner is up and the
  // panel is waiting on the printer for something.
  enum class RunState { idle, testing, analysing };

  // One axis's worth of everything. This file used to carry two of every
  // member and two of every branch, which is how a fix could land on the X
  // half and not the Y half and nobody notice.
  struct Axis {
    const char *name;     // "X", for the user
    const char *csv;      // where TEST_RESONANCES writes its data
    const char *png;      // basename of the plot the analysis draws
    bool pending;         // queued for calibration but not sent yet
    RunState run;
    uint32_t since;       // lv_tick when run last changed, for the watchdog
    bool shaper_known;    // the configured type is one of ours
    double freq;
    std::string shaper;
    std::string plot_path;  // "A:" prefixed, once the plot is on disk
    json result;            // last shapers/best payload
  };

  Axis &shown_axis();
  void show_axis(size_t idx);

  // Which axes Calibrate will run, from the axis row's selection.
  bool wants(size_t idx) const;

  void start_next_axis();
  void start_run(size_t idx);
  void finish_run(size_t idx);

  // Give up on runs in flight and say why. Not per axis: while an analysis is
  // in flight there is no telling from the message which axis it belongs to,
  // and a spinner nothing ever clears is the worse outcome. analysing_only
  // spares an axis whose test has not run yet.
  void abandon_runs(const std::string &why, bool analysing_only = false);

  void apply_result(Axis &axis, json &res);
  void request_analysis(Axis &axis);

  // Fixed width columns in the mono font, in shaper_defs order. The old table
  // padded its header row only and separated with tabs, which LVGL renders as
  // exactly two spaces rather than a tab stop, so any three digit frequency or
  // two digit vibration figure shifted every column after it.
  std::string render_table(const json &result) const;
  std::string render_headline(const Axis &axis) const;

  void set_status(const std::string &text);
  void set_frequency(double hz);
  void update_available();
  void update_view();

  // Point the shaper row at a name. Leaves it alone and returns false when the
  // name is not one of ours: lv_dropdown_set_selected used to clamp an out of
  // range index to the last option, so an unrecognised shaper silently became
  // 3hump_ei and Save wrote that back.
  bool select_shaper(const std::string &shaper);

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;

  Selector axis_sel;
  lv_obj_t *headline;

  lv_obj_t *plot_cont;
  lv_obj_t *plot;
  lv_obj_t *table;
  lv_obj_t *spinner;

  Selector view_sel;
  lv_obj_t *status;

  Selector shaper_sel;
  lv_obj_t *freq_cont;
  lv_obj_t *freq_slider;
  lv_obj_t *freq_label;

  lv_obj_t *button_cont;
  ButtonContainer calibrate_btn;
  ButtonContainer save_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;

  std::array<Axis, 2> axes;
  size_t shown;

  // Whether the analysis currently bracketed by gcode_shell_command's own
  // "Running Command" and "finished" lines has printed a result yet.
  bool analysis_produced_result;

  lv_timer_t *watchdog;

  static std::vector<std::string> shapers;
};

#endif // __INPUTSHAPER_PANEL_H__
