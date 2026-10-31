#include "manual_z_panel.h"
#include "state.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(home);
LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(z_closer);
LV_IMG_DECLARE(z_farther);
LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(back);

ManualZPanel::ManualZPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , info_cont(lv_obj_create(panel_cont))
  , steps_label(lv_label_create(info_cont))
  , position_label(lv_label_create(info_cont))
  , offset_label(lv_label_create(info_cont))
  , next_print_label(lv_label_create(info_cont))
  , home_xy_btn(panel_cont, &home, "1. Home XY", &ManualZPanel::_handle_callback, this)
  // Always prompted. Declaring Z is the step that takes away the only thing
  // that would have stopped the bed at the nozzle. One line, because the
  // prompt box is a fixed size and a second line runs under its buttons on a
  // small screen; the warning is in the step text instead. See audit M11.
  , start_btn(panel_cont, &resume, "2. Start", &ManualZPanel::_handle_callback, this,
	      "Take the bed to be at the bottom?",
	      [this]() { this->start(); }, false)
  , zup_btn(panel_cont, &z_farther, "Z+", &ManualZPanel::_handle_callback, this)
  , zdown_btn(panel_cont, &z_closer, "Z-", &ManualZPanel::_handle_callback, this)
  , zero_btn(panel_cont, &home_z, "4. Set Z=0", &ManualZPanel::_handle_callback, this,
	     "Make this nozzle height Z=0?",
	     [this]() { this->set_zero(); }, false)
  , prepare_btn(panel_cont, &print, "5. Skip Homing", &ManualZPanel::_handle_callback, this)
  , emergency_btn(panel_cont, &emergency, "Stop", &ManualZPanel::_handle_callback, this,
		  "Do you want to emergency stop?",
		  [&websocket_client]() {
		    spdlog::debug("emergency stop pressed");
		    websocket_client.send_jsonrpc("printer.emergency_stop");
		  })
  , back_btn(panel_cont, &back, "Back", &ManualZPanel::_handle_callback, this)
  // Coarse steps for the long climb from the bottom, fine ones for the paper.
  , step_selector(panel_cont, "3. Raise the bed (mm)",
		  {"10", "1", "0.1", "0.05", "0.01", ""}, 1, 58, 15,
		  &ManualZPanel::_handle_selector, this)
  , zeroed(false)
{
  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(4), LV_GRID_FR(4), LV_GRID_FR(3),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);

  // The steps run down the columns in the order they are taken, with jogging
  // between Start and Set Z=0, and what to do next on the right.
  lv_obj_set_grid_cell(home_xy_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(start_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(zup_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(zdown_btn.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(zero_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(prepare_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(step_selector.get_container(), LV_GRID_ALIGN_CENTER, 0, 3, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(info_cont, LV_GRID_ALIGN_STRETCH, 3, 2, LV_GRID_ALIGN_STRETCH, 0, 2);
  lv_obj_set_grid_cell(emergency_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 2, 1);

  lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (lv_obj_t *l : {steps_label, position_label, offset_label, next_print_label}) {
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
  // The step to take is what matters most, so it gets the room and the rest
  // sits underneath it.
  lv_obj_set_flex_grow(steps_label, 1);

  ws.register_notify_update(this);
}

ManualZPanel::~ManualZPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void ManualZPanel::foreground() {
  // Same convention as the homing panel, which carries the reasoning.
  auto v = Config::get_instance()->get_json("/invert_z_arrows");
  bool inverted = !v.is_null() && v.template get<bool>();
  zup_btn.set_image(inverted ? &z_closer : &z_farther);
  zdown_btn.set_image(inverted ? &z_farther : &z_closer);

  jog_refusal.clear();
  refresh();
  lv_obj_move_foreground(panel_cont);
}

void ManualZPanel::consume(const json &j) {
  // Only what the panel shows. Temperatures arrive every second, and redrawing
  // on those would clear a refused jog's message before it could be read.
  static const json::json_pointer watched[] = {
    "/params/0/toolhead/homed_axes"_json_pointer,
    "/params/0/toolhead/position/2"_json_pointer,
    "/params/0/print_stats/state"_json_pointer,
    "/params/0/gcode_move/homing_origin/2"_json_pointer,
    "/params/0/gcode_macro START_PRINT/prepare"_json_pointer,
  };
  for (const auto &ptr : watched) {
    if (!KUtils::value_at(j, ptr).is_null()) {
      std::lock_guard<std::mutex> lock(lv_lock);
      jog_refusal.clear();
      refresh();
      return;
    }
  }
}

void ManualZPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == home_xy_btn.get_container()) {
    // On a K1 this goes through homing_override to _HOME_X and _HOME_Y, which
    // home on the steppers' stallguard and never touch the load cell.
    ws.gcode_script("G28 X Y");
  } else if (btn == zup_btn.get_container() || btn == zdown_btn.get_container()) {
    const char *step = lv_btnmatrix_get_btn_text(step_selector.get_selector(),
						 step_selector.get_selected_idx());
    double distance = KUtils::parse_double(step, 0.0);
    jog(btn == zup_btn.get_container() ? distance : -distance);
  } else if (btn == prepare_btn.get_container()) {
    // Creality's own flag. START_PRINT skips CX_ROUGH_G28, ACCURATE_G28 and the
    // bed mesh probe while it is set, and END_PRINT clears it, so it lasts for
    // exactly one print.
    ws.gcode_script("PRINT_PREPARED");
  } else if (btn == emergency_btn.get_container()) {
    // Only reached with the emergency stop prompt turned off in System. With it
    // on, which is the default, the prompt's own callback sends this instead.
    spdlog::debug("emergency stop pressed");
    ws.send_jsonrpc("printer.emergency_stop");
  } else if (btn == back_btn.get_container()) {
    lv_obj_move_background(panel_cont);
  }
}

void ManualZPanel::handle_selector(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    step_selector.set_selected_idx(lv_btnmatrix_get_selected_btn(selector));
  }
}

void ManualZPanel::start() {
  State *s = State::get_instance();
  auto &min = s->get_data("/printer_state/toolhead/axis_minimum"_json_pointer);
  auto &max = s->get_data("/printer_state/toolhead/axis_maximum"_json_pointer);
  if (!min.is_array() || !max.is_array() || min.size() < 3 || max.size() < 3) {
    return;
  }

  // The bottom of travel is the one claim that cannot hurt anything: from
  // there Klipper allows no move further down, so whatever the real height,
  // the bed can only be raised from where it already is. The nozzle goes over
  // the middle of the bed for the paper.
  zeroed = false;
  double zmax = max[2].template get<double>();
  double cx = (min[0].template get<double>() + max[0].template get<double>()) / 2;
  double cy = (min[1].template get<double>() + max[1].template get<double>()) / 2;
  ws.gcode_script(fmt::format("SET_KINEMATIC_POSITION Z={:.3f}\n"
			      "SAVE_GCODE_STATE NAME=guppy_manual_z\n"
			      "G90\n"
			      "G1 X{:.1f} Y{:.1f} F6000\n"
			      "RESTORE_GCODE_STATE NAME=guppy_manual_z",
			      zmax, cx, cy));
}

void ManualZPanel::set_zero() {
  // Lifted afterwards so the paper comes free and nothing drags across the bed
  // on the next move.
  zeroed = true;
  ws.gcode_script("SET_KINEMATIC_POSITION Z=0\n"
		  "SAVE_GCODE_STATE NAME=guppy_manual_z\n"
		  "G91\n"
		  "G1 Z2 F600\n"
		  "RESTORE_GCODE_STATE NAME=guppy_manual_z");
  refresh();
}

void ManualZPanel::jog(double distance) {
  State *s = State::get_instance();
  auto &pos = s->get_data("/printer_state/toolhead/position/2"_json_pointer);
  auto &min = s->get_data("/printer_state/toolhead/axis_minimum/2"_json_pointer);
  auto &max = s->get_data("/printer_state/toolhead/axis_maximum/2"_json_pointer);
  if (!pos.is_number() || !min.is_number() || !max.is_number()) {
    return;
  }

  // Checked here because Klipper's own refusal comes partway through the
  // script, after G91 and before RESTORE_GCODE_STATE, which leaves the printer
  // in relative positioning for whatever runs next.
  double target = pos.template get<double>() + distance;
  if (target < min.template get<double>() || target > max.template get<double>()) {
    jog_refusal = fmt::format("Not sent: Z would be {:g}, outside {:g} to {:g}.",
			      target, min.template get<double>(), max.template get<double>());
    spdlog::debug("manual z: {}", jog_refusal);
    refresh();
    return;
  }

  ws.gcode_script(fmt::format("SAVE_GCODE_STATE NAME=guppy_manual_z\n"
			      "G91\n"
			      "G1 Z{:.3f} F600\n"
			      "RESTORE_GCODE_STATE NAME=guppy_manual_z", distance));
}

void ManualZPanel::refresh() {
  State *s = State::get_instance();

  auto &h = s->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
  std::string homed = h.is_string() ? h.template get<std::string>() : "";
  bool xy_homed = homed.find('x') != std::string::npos && homed.find('y') != std::string::npos;
  bool z_homed = homed.find('z') != std::string::npos;
  if (!z_homed) {
    zeroed = false;
  }

  auto &ps = s->get_data("/printer_state/print_stats/state"_json_pointer);
  std::string pstate = ps.is_string() ? ps.template get<std::string>() : "";
  bool printing = pstate == "printing" || pstate == "paused";

  // SET_KINEMATIC_POSITION only exists with force_move enabled. Creality's
  // version marks X, Y and Z homed whatever it is given, which is why Start
  // also waits for X and Y to be homed for real.
  auto &fm = s->get_data("/printer_state/configfile/settings/force_move/enable_force_move"_json_pointer);
  bool can_declare = fm.is_boolean() && fm.template get<bool>();

  auto &prepare = s->get_data("/printer_state/gcode_macro START_PRINT/prepare"_json_pointer);
  bool can_prepare = KUtils::has_gcode_macro("PRINT_PREPARED") && prepare.is_number();
  bool prepared = can_prepare && prepare.template get<int>() != 0;

  auto set = [](ButtonContainer &b, bool on) { if (on) b.enable(); else b.disable(); };
  set(home_xy_btn, !printing);
  set(start_btn, !printing && xy_homed && can_declare);
  set(zup_btn, !printing && z_homed);
  set(zdown_btn, !printing && z_homed);
  set(zero_btn, !printing && z_homed && can_declare);
  set(prepare_btn, !printing && zeroed && can_prepare && !prepared);

  auto &z = s->get_data("/printer_state/toolhead/position/2"_json_pointer);
  lv_label_set_text(position_label, z_homed && z.is_number()
		    ? fmt::format("Z {:.3f} mm", z.template get<double>()).c_str()
		    : "Z not homed");

  // The Fine Tune offset still applies on top of the zero set here, so it is
  // worth seeing before a print.
  auto &o = s->get_data("/printer_state/gcode_move/homing_origin/2"_json_pointer);
  lv_label_set_text(offset_label, o.is_number()
		    ? fmt::format("Z offset {}", KUtils::short_measure(o.template get<double>(), "mm")).c_str()
		    : "");

  lv_label_set_text(next_print_label, !can_prepare
		    ? "No PRINT_PREPARED macro, so the next print homes Z itself."
		    : prepared ? "Next print skips Z homing and the bed mesh probe."
		    : "Next print homes Z on its probe.");

  std::string steps;
  if (printing) {
    steps = "Not while a print is running.";
  } else if (!can_declare) {
    steps = "Needs [force_move] with enable_force_move: True in printer.cfg.";
  } else if (!jog_refusal.empty()) {
    steps = jog_refusal;
  } else if (!xy_homed) {
    steps = "For when the Z probe has failed. Home X and Y first. Z is left alone.";
  } else if (!z_homed) {
    steps = "Start puts the nozzle over the middle of the bed and takes the bed "
            "to be at the bottom, so it can only be raised.";
  } else if (!zeroed) {
    steps = "Raise the bed with Z- until paper under the nozzle drags. "
            "Nothing stops it there: go slowly. Then Set Z=0.";
  } else if (can_prepare && !prepared) {
    steps = "Z=0 is set. Skip Homing makes the next print keep it.";
  } else {
    steps = "Z=0 is set. Fine Tune's Z offset still adjusts the first layer.";
  }
  lv_label_set_text(steps_label, steps.c_str());
}
