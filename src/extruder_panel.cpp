#include "extruder_panel.h"
#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <limits>

namespace {
  // Don't reheat when we are already there. A couple of degrees of slack keeps
  // a normal PID wobble from counting as "not at temperature".
  const int HEAT_HYSTERESIS_C = 3;

  // Above this many options the row is split in two, otherwise buttons get too
  // narrow to hit on a 480px wide panel.
  const size_t MAX_OPTIONS_PER_ROW = 7;

  std::string format_option(double v) {
    if (v == (long long)v) {
      return fmt::format("{}", (long long)v);
    }
    return fmt::format("{:g}", v);
  }

  // Turn a config array of numbers into selector labels plus the values behind
  // them. Labels may contain a "\n" row break; values never do, so value
  // indexes line up with LVGL button indexes.
  void build_options(const std::string &config_key,
		     const std::vector<double> &fallback,
		     std::vector<std::string> &labels,
		     std::vector<double> &values) {
    values.clear();
    labels.clear();

    Config *conf = Config::get_instance();
    const json &configured = conf->get_json(conf->df() + config_key);

    if (configured.is_array()) {
      for (const auto &el : configured) {
	if (el.is_number()) {
	  values.push_back(el.template get<double>());
	} else {
	  spdlog::warn("ignoring non-numeric entry in {}: {}", config_key, el.dump());
	}
      }
    }

    if (values.empty()) {
      spdlog::info("{} missing or unusable, using built in defaults", config_key);
      values = fallback;
    }

    size_t split_at = values.size() > MAX_OPTIONS_PER_ROW
      ? (values.size() + 1) / 2
      : values.size();

    for (size_t i = 0; i < values.size(); i++) {
      if (i == split_at) {
	labels.push_back("\n");
      }
      labels.push_back(format_option(values[i]));
    }
  }
}

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(spoolman_img);
LV_IMG_DECLARE(extrude_img);
LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(unload_filament_img);
LV_IMG_DECLARE(load_filament_img);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(cooldown_img);

ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
			     std::mutex &lock,
			     Numpad &numpad,
			     SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , spoolman_panel(sm)
  , extruder_temp(ws, panel_cont, &extruder, 150,
	  "Extruder", lv_palette_main(LV_PALETTE_RED), false, true, numpad, "extruder", NULL, NULL)
  , temp_selector(panel_cont, "Extruder Temperature (C)",
		  {"180", "190", "200", "210", "220", "230", "240", ""}, 6, &ExtruderPanel::_handle_callback, this)
  , length_selector(panel_cont, "Extrude Length (mm)",
		    {"5", "10", "15", "20", "25", "30", "35", ""}, 1, &ExtruderPanel::_handle_callback, this)
  , speed_selector(panel_cont, "Extrude Speed (mm/s)",
		   {"1", "2", "5", "10", "25", "35", "50", ""}, 2, &ExtruderPanel::_handle_callback, this)
  , rightside_btns_cont(lv_obj_create(panel_cont))
  , leftside_btns_cont(lv_obj_create(panel_cont))
  , load_btn(leftside_btns_cont, &load_filament_img, "Load", &ExtruderPanel::_handle_callback, this)
  , unload_btn(leftside_btns_cont, &unload_filament_img, "Unload", &ExtruderPanel::_handle_callback, this)
  , cooldown_btn(leftside_btns_cont, &cooldown_img, "Cooldown", &ExtruderPanel::_handle_callback, this)
  , spoolman_btn(rightside_btns_cont, &spoolman_img, "Spoolman", &ExtruderPanel::_handle_callback, this)
  , extrude_btn(rightside_btns_cont, &extrude_img, "Extrude", &ExtruderPanel::_handle_callback, this)
  , retract_btn(rightside_btns_cont, &retract_img, "Retract", &ExtruderPanel::_handle_callback, this)
  , back_btn(rightside_btns_cont, &back, "Back", &ExtruderPanel::_handle_callback, this)
  , load_filament_macro("LOAD_FILAMENT")
  , unload_filament_macro("UNLOAD_FILAMENT")
  , cooldown_macro("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0")
  , current_temp(-1)
  , current_target(-1)
  , min_extrude_temp(0.0)
{
  // The selectors above are built with the historical option lists so they are
  // always valid. The real lists come from config and replace them here.
  {
    std::vector<std::string> labels;
    build_options("extrude_temps", {180, 190, 200, 210, 220, 230, 240}, labels, temp_values);
    temp_selector.set_options(labels, 6);

    build_options("extrude_lengths", {5, 10, 15, 20, 25, 30, 35}, labels, length_values);
    length_selector.set_options(labels, 1);

    build_options("extrude_speeds", {1, 2, 5, 10, 25, 35, 50}, labels, speed_values);
    speed_selector.set_options(labels, 3);
  }

  Config *conf = Config::get_instance();
  auto df = conf->get_json("/default_printer");
  if (!df.empty()) {
    auto v = conf->get_json(conf->df() + "default_macros/load_filament");
    if (!v.is_null()) {
      load_filament_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/unload_filament");
    if (!v.is_null()) {
      unload_filament_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/cooldown");
    if (!v.is_null()) {
      cooldown_macro = v.template get<std::string>();
    }
  }

  lv_obj_move_background(panel_cont);
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);  
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  lv_obj_set_size(rightside_btns_cont, LV_PCT(20), LV_PCT(100));  
  lv_obj_set_flex_flow(rightside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rightside_btns_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(rightside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(leftside_btns_cont, LV_PCT(20), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(leftside_btns_cont, 15, 0);
  lv_obj_set_flex_flow(leftside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(leftside_btns_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(leftside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);
  
  spoolman_btn.disable();  

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(6), LV_GRID_FR(6), LV_GRID_FR(6),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(7), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
  
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);
  lv_obj_add_flag(extruder_temp.get_sensor(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(extruder_temp.get_sensor(), LV_ALIGN_TOP_LEFT, 50, 0);

  // lv_obj_set_size(extruder_temp.get_sensor(), 350, 60);
  // col 0
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 0, 2);
  // lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(unload_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(cooldown_btn.get_container(), LV_GRID_ALIGN_END, 0, 1, LV_GRID_ALIGN_END, 2, 2);

  lv_obj_set_grid_cell(leftside_btns_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 3);
  
  // col 1
  // lv_obj_set_grid_cell(extruder_temp.get_sensor(), LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(speed_selector.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(length_selector.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(temp_selector.get_container(), LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 3, 1);
  
  // col 2
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 0, 2);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);

  lv_obj_set_grid_cell(rightside_btns_cont, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 0, 4);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);
  

  ws.register_notify_update(this);    
}

ExtruderPanel::~ExtruderPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
}

void ExtruderPanel::consume(json& j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto target_value = j["/params/0/extruder/target"_json_pointer];
  if (!target_value.is_null()) {
    int target = target_value.template get<int>();
    current_target = target;
    extruder_temp.update_target(target);
  }

  auto temp_value = j["/params/0/extruder/temperature"_json_pointer];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    current_temp = value;
    extruder_temp.update_value(value);
  }
}

// Signature matches the other panel init hooks dispatched from MainPanel::init.
// The limits come from State rather than the payload, so j is unused here.
void ExtruderPanel::init(json &) {
  apply_extrude_limits();
}

void ExtruderPanel::apply_extrude_limits() {
  State *s = State::get_instance();
  auto v = s->get_data("/printer_state/configfile/settings/extruder"_json_pointer);
  if (v.is_null()) {
    spdlog::debug("no extruder config reported, leaving all options enabled");
    return;
  }

  // A hotend that cannot go above max_temp will refuse the target outright, so
  // grey those options out rather than letting the command error.
  if (v.contains("max_temp") && v["max_temp"].is_number()) {
    double max_temp = v["max_temp"].template get<double>();
    for (size_t i = 0; i < temp_values.size(); i++) {
      temp_selector.set_enabled(i, temp_values[i] <= max_temp);
    }
    spdlog::debug("extruder max_temp {}", max_temp);
  }

  // Klipper rejects a single extrude only move longer than this. The default
  // is 50mm, so the longer purge options are unusable on a stock config.
  if (v.contains("max_extrude_only_distance") && v["max_extrude_only_distance"].is_number()) {
    double max_len = v["max_extrude_only_distance"].template get<double>();
    for (size_t i = 0; i < length_values.size(); i++) {
      length_selector.set_enabled(i, length_values[i] <= max_len);
    }
    spdlog::debug("extruder max_extrude_only_distance {}", max_len);
  }

  if (v.contains("min_extrude_temp") && v["min_extrude_temp"].is_number()) {
    min_extrude_temp = v["min_extrude_temp"].template get<double>();
    spdlog::debug("extruder min_extrude_temp {}", min_extrude_temp);
  }

  refresh_extrude_buttons();
}

// Extruding below min_extrude_temp is refused by Klipper, so disable the two
// buttons that would do it rather than sending a doomed move. Load and unload
// are left alone, since those run macros that handle their own heating.
void ExtruderPanel::refresh_extrude_buttons() {
  uint32_t idx = temp_selector.get_selected_idx();
  if (idx >= temp_values.size()) {
    return;
  }

  if (temp_values[idx] < min_extrude_temp) {
    extrude_btn.disable();
    retract_btn.disable();
  } else {
    extrude_btn.enable();
    retract_btn.enable();
  }
}

// direction is 1 to extrude, -1 to retract.
void ExtruderPanel::send_extrude_move(int direction) {
  const char *temp = temp_selector.selected_text();
  const char *len = length_selector.selected_text();
  const char *speed = speed_selector.selected_text();

  if (temp == nullptr || len == nullptr || speed == nullptr) {
    spdlog::error("extrude aborted, a selector has no valid selection");
    return;
  }

  // Guarded parses. These come from config, so a bad entry must not take the
  // process down. A feedrate of zero is rejected by Klipper, so fall back to a
  // slow but valid speed rather than sending F0.
  double speed_mms = KUtils::parse_double(speed, 5.0);
  if (speed_mms <= 0.0) {
    spdlog::warn("extrude speed '{}' is not positive, using 5 mm/s", speed);
    speed_mms = 5.0;
  }
  double feedrate = speed_mms * 60.0;

  std::string gcode;

  // Only reheat when we are not already sitting at the selected temperature.
  // Checking the reading alone is not enough: at 240 with a target of 0 the
  // hotend is on its way down, and extruding into it would be wrong.
  int wanted = KUtils::parse_int(temp, -1);
  bool at_temp = wanted >= 0
    && current_target == wanted
    && current_temp >= wanted - HEAT_HYSTERESIS_C;

  if (!at_temp) {
    gcode += fmt::format("M109 S{}\n", temp);
  } else {
    spdlog::debug("already at {}C with target {}, skipping M109", current_temp, current_target);
  }

  // M83 changes the extrusion mode globally and nothing used to change it
  // back, so purging during a paused print left the rest of that print running
  // in relative extrusion. Save and restore around the move instead.
  // RESTORE_GCODE_STATE defaults to MOVE=0, so it restores the mode without
  // moving the toolhead.
  gcode += "SAVE_GCODE_STATE NAME=guppy_extrude\n";
  gcode += "M83\n";
  gcode += fmt::format("G1 E{}{} F{:g}\n", direction < 0 ? "-" : "", len, feedrate);
  gcode += "RESTORE_GCODE_STATE NAME=guppy_extrude";

  ws.gcode_script(gcode);
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  spdlog::trace("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char * v = lv_btnmatrix_get_btn_text(selector, idx);

    if (selector == temp_selector.get_selector()) {
      temp_selector.set_selected_idx(idx);
      refresh_extrude_buttons();
    }

    if (selector == length_selector.get_selector()) {
      length_selector.set_selected_idx(idx);
    }

    if (selector == speed_selector.get_selector()) {
      speed_selector.set_selected_idx(idx);
    }

    spdlog::trace("selector {} {} {}, {} {} {}", fmt::ptr(selector), idx, v,
		  fmt::ptr(temp_selector.get_selector()),
		  fmt::ptr(length_selector.get_selector()),
		  fmt::ptr(speed_selector.get_selector()));
    
  } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(panel_cont);
    }

    if (btn == extrude_btn.get_container()) {
      send_extrude_move(1);
    }

    if (btn == retract_btn.get_container()) {
      send_extrude_move(-1);
    }

    if (btn == unload_btn.get_container()) {
      if (unload_filament_macro == "_GUPPY_QUIT_MATERIAL") {
        const char *temp = temp_selector.selected_text();
        if (temp == nullptr) {
          spdlog::error("unload aborted, no temperature selected");
          return;
        }
        ws.gcode_script(fmt::format("{} EXTRUDER_TEMP={}", unload_filament_macro, temp));
      } else {
        ws.gcode_script(unload_filament_macro);
      }
    }

    if (btn == load_btn.get_container()) {
      if (load_filament_macro == "_GUPPY_LOAD_MATERIAL") {
        const char *temp = temp_selector.selected_text();
        const char *len = length_selector.selected_text();
        if (temp == nullptr || len == nullptr) {
          spdlog::error("load aborted, no temperature or length selected");
          return;
        }
        ws.gcode_script(fmt::format("{} EXTRUDER_TEMP={} EXTRUDE_LEN={}", load_filament_macro, temp, len));
      } else {
        ws.gcode_script(load_filament_macro);
      }
    }

    if (btn == cooldown_btn.get_container()) {
      ws.gcode_script(cooldown_macro);
    }

    if (btn == spoolman_btn.get_container()) {
      spoolman_panel.foreground();
    }
  }
}
