#include "belts_calibration_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(inputshaper_img);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(back);

#define BELTS_PNG "belts_calibration.png"

// What TEST_RESONANCES names its output, and so what Klipper reports written
// and what graph_belts.py is handed. Klipper builds the name from the axis and
// the NAME parameter, and preserves the case of NAME, which graph_belts.py
// compares against upper case A and B.
#define BELTS_CSV_B "/tmp/raw_data_axis=1.000,1.000_B.csv"
#define BELTS_CSV_A "/tmp/raw_data_axis=1.000,-1.000_A.csv"

std::vector<std::string> BeltsCalibrationPanel::axes = {
  "x",
  "y",
  "a",
  "b"
};

BeltsCalibrationPanel::BeltsCalibrationPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , graph_cont(lv_obj_create(cont))
  , graph(lv_img_create(graph_cont))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , excite_control(lv_obj_create(cont))
  , excite_slider(lv_slider_create(excite_control))
  , excite_label(lv_label_create(excite_control))
  , excite_dd(lv_dropdown_create(excite_control))
  , status_label(lv_label_create(cont))
  , button_cont(lv_obj_create(cont))
  , calibrate_btn(button_cont, &resume, "Shake Belts", &BeltsCalibrationPanel::_handle_callback, this)
  , excite_btn(button_cont, &inputshaper_img, "Excitate", &BeltsCalibrationPanel::_handle_callback, this)
  , emergency_btn(button_cont, &emergency, "Stop", &BeltsCalibrationPanel::_handle_callback, this,
		  "Do you want to emergency stop?",
		  [&c]() {
		    spdlog::debug("emergency stop pressed");
		    c.send_jsonrpc("printer.emergency_stop");
		  })
  , back_btn(button_cont, &back, "Back", &BeltsCalibrationPanel::_handle_callback, this)
  , image_fullsized(false)
  , watchdog(NULL)
  , run(RunState::idle)
  , run_since(0)
  , analysis_produced_result(false)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_set_style_pad_all(graph_cont, 0, 0);
  lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(graph_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(graph_cont, &BeltsCalibrationPanel::_handle_image_clicked,
		      LV_EVENT_CLICKED, this);
  lv_obj_set_size(graph_cont, LV_PCT(50), LV_PCT(50));

  lv_img_set_zoom(graph, 100);
  lv_obj_center(graph);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(spinner, 100, 100);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

  // excite controls
  lv_obj_t *label = lv_label_create(excite_control);
  lv_label_set_text(label, "Excite Frequency Control");
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_clear_flag(excite_control, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(excite_control, LV_PCT(80));
  lv_obj_align(excite_slider, LV_ALIGN_LEFT_MID, 75 * scale, 0);
  lv_obj_set_width(excite_slider, LV_PCT(60));
  lv_slider_set_range(excite_slider, 10, 1400);

  lv_obj_add_event_cb(excite_slider, &BeltsCalibrationPanel::_handle_update_slider,
		      LV_EVENT_VALUE_CHANGED, this);
  
  lv_obj_align_to(excite_label, excite_slider, LV_ALIGN_BOTTOM_MID, 0, 35 * hscale);
  lv_label_set_text(excite_label, "1 hz");
  
  lv_dropdown_set_options(excite_dd, fmt::format("{}", fmt::join(axes, "\n")).c_str());
  lv_obj_align(excite_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_size(button_cont, LV_PCT(100), LV_SIZE_CONTENT);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(4), LV_GRID_FR(1), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  
  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_grid_cell(graph_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_cell(spinner, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(excite_control, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

  lv_obj_set_grid_cell(button_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 2, 1);

  // A spinner on its own says something is happening and nothing about what,
  // and a belt sweep takes minutes. This says which belt is being shaken, and
  // it is also where a failure gets reported, since the only thing that used to
  // clear the spinner was success.
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(status_label, LV_PCT(90));
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_bottom(status_label, 10 * hscale, 0);
  lv_label_set_text(status_label, "");
  // Bottom of the graph row, so it sits under the spinner while a run is going
  // and under the plot afterwards, rather than on top of the excite controls.
  lv_obj_set_grid_cell(status_label, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_END, 0, 1);

  watchdog = lv_timer_create(&BeltsCalibrationPanel::_handle_watchdog, 1000, this);
  lv_timer_pause(watchdog);

  ws.register_method_callback("notify_gcode_response",
			      "BeltsCalibrationPanel",
			      [this](json& d) { this->handle_macro_response(d); });

  // Klipper going away mid run is the one failure that produces no gcode
  // response at all, so without this the watchdog would be the only thing left
  // to catch it and that takes fifteen minutes.
  ws.register_method_callback("notify_klippy_disconnected",
			      "BeltsCalibrationPanel",
			      [this](json&) { this->handle_klippy_gone(); });
  ws.register_method_callback("notify_klippy_shutdown",
			      "BeltsCalibrationPanel",
			      [this](json&) { this->handle_klippy_gone(); });
}

BeltsCalibrationPanel::~BeltsCalibrationPanel() {
  if (watchdog != NULL) {
    lv_timer_del(watchdog);
    watchdog = NULL;
  }

  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void BeltsCalibrationPanel::foreground() {
  update_available();
  lv_obj_move_foreground(cont);
}

// Klipper abandons the rest of a script at the first command it will not run,
// so a button that sends something this printer lacks does worse than nothing.
// Both of these live in configfile rather than in printer.objects.list, see
// KUtils::has_config_section.
void BeltsCalibrationPanel::update_available() {
  bool can_test = KUtils::has_config_section("resonance_tester");
  bool can_analyse = KUtils::has_config_section("gcode_shell_command guppy_belts_calibration");

  std::vector<std::string> missing;
  if (!can_test) {
    missing.push_back("[resonance_tester]");
  }
  if (!can_analyse) {
    missing.push_back("[gcode_shell_command guppy_belts_calibration]");
  }

  if (missing.empty()) {
    calibrate_btn.enable();
    excite_btn.enable();
    if (run == RunState::idle) {
      set_status("");
    }
    return;
  }

  // Name what is missing rather than hiding the control, so someone on a
  // printer without an accelerometer can tell why it will not do anything.
  calibrate_btn.disable();
  if (!can_test) {
    excite_btn.disable();
  }
  set_status(fmt::format("This printer has no {}, so belt calibration is not "
			 "available.", fmt::join(missing, " or ")));
}

void BeltsCalibrationPanel::set_status(const std::string &text) {
  lv_label_set_text(status_label, text.c_str());
}

void BeltsCalibrationPanel::start_test(RunState state, const std::string &axis,
				       const char *name, const std::string &status) {
  // Home inside the same script as the test rather than as its own. Klipper
  // abandons the rest of a script at the first command it will not run, so a
  // failed G28 stops the test instead of shaking an unhomed toolhead.
  std::string script = KUtils::is_homed() ? "" : "G28\n";
  script += fmt::format("TEST_RESONANCES AXIS={} OUTPUT=raw_data NAME={}\nM400",
			axis, name);
  ws.gcode_script(script);

  run = state;
  run_since = lv_tick_get();
  analysis_produced_result = false;
  lv_timer_resume(watchdog);
  set_status(status);
}

void BeltsCalibrationPanel::request_analysis() {
  auto config_root = KUtils::get_root_path("config");
  auto png_path = fmt::format("{}/{}",
			      config_root.length() > 0 ? config_root : "/tmp", BELTS_PNG);

  // Ask matplotlib for the space the plot actually gets, at its default 100
  // dpi, rather than drawing at screen size and shrinking it afterwards, which
  // shrinks the labels and the legend with it.
  lv_obj_update_layout(cont);
  lv_coord_t cw = lv_obj_get_content_width(graph_cont);
  lv_coord_t ch = lv_obj_get_content_height(graph_cont);
  if (cw <= 0 || ch <= 0) {
    cw = lv_disp_get_physical_hor_res(NULL);
    ch = lv_disp_get_physical_ver_res(NULL);
  }

  run = RunState::analysing;
  run_since = lv_tick_get();
  analysis_produced_result = false;
  set_status("Comparing the two belts. This takes a few minutes.");

  ws.gcode_script(fmt::format(
      "RUN_SHELL_COMMAND CMD=guppy_belts_calibration PARAMS={:?}",
      fmt::format("-w {:.2f} -l {:.2f} -n -o {} -k /usr/share/klipper {} {}",
		  cw / 100.0, ch / 100.0, png_path, BELTS_CSV_A, BELTS_CSV_B)));
}

void BeltsCalibrationPanel::finish_run(const std::string &png_path) {
  run = RunState::idle;
  lv_timer_pause(watchdog);

  lv_img_set_src(graph, png_path.c_str());
  lv_obj_clear_flag(graph, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(spinner);
  set_status("");
}

// Every ending other than success used to leave the spinner turning with Back
// as the only escape, and it came back in that state.
void BeltsCalibrationPanel::abandon_run(const std::string &why) {
  spdlog::warn("belts calibration abandoned: {}", why);
  run = RunState::idle;
  lv_timer_pause(watchdog);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(spinner);
  set_status(why);
}

void BeltsCalibrationPanel::check_timeouts() {
  // Only fires when nothing at all is coming back. A sweep takes a few minutes
  // and gcode_shell_command's own timeout is 600 s, so past this it has stopped
  // rather than slowed down.
  const uint32_t limit = 15 * 60 * 1000;

  if (run != RunState::idle && lv_tick_elaps(run_since) > limit) {
    std::lock_guard<std::mutex> lock(lv_lock);
    abandon_run("The printer stopped reporting on the belt test. "
		"Check the Klipper log.");
  }
}

void BeltsCalibrationPanel::handle_klippy_gone() {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (run != RunState::idle) {
    abandon_run("Klipper disconnected while the belt test was running.");
  }
}

void BeltsCalibrationPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == calibrate_btn.get_container()) {
    if (run != RunState::idle) {
      return;
    }

    lv_obj_add_flag(graph, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(graph, NULL);    
    lv_obj_invalidate(graph);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(spinner);

    // One sweep at a time, driven from here, rather than the macro's two back
    // to back. The macro is still there for anyone running it by hand, and it
    // is still what defines the two axes and the names, but sending both at
    // once meant the second started before the first CSV had been flushed and
    // two full datasets were held at once. On a K1 with 118MB free that shows
    // up as an MCU timeout or a move queue overflow part way through the
    // second belt, which is upstream #104. Same fault, and same fix, as
    // 6060036 for the input shaper.
    start_test(RunState::testing_b, "1,1", "B",
	       "Shaking belt B. This takes a few minutes.");

  } else if (btn == excite_btn.get_container()) {
    double excite_hz = (double)lv_slider_get_value(excite_slider) / 10.0 + 1; // [x-1, x+1]
    char excite_buf[10];
    lv_dropdown_get_selected_str(excite_dd, excite_buf, sizeof(excite_buf));

    if (!KUtils::is_homed()) {
      ws.gcode_script("G28");
    }
    ws.gcode_script(fmt::format("GUPPY_EXCITATE_AXIS_AT_FREQ FREQUENCY={} AXIS={}", excite_hz, excite_buf));

  } else if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
  } else if (btn == emergency_btn.get_container()) {
    ws.send_jsonrpc("printer.emergency_stop");
  }
}

void BeltsCalibrationPanel::handle_image_clicked(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *clicked = lv_event_get_target(e);

    if (clicked == graph_cont) {
      if (image_fullsized) {
	lv_img_set_zoom(graph, 100);
	lv_obj_set_size(graph_cont, LV_PCT(50), LV_PCT(50));
	lv_obj_clear_flag(graph_cont, LV_OBJ_FLAG_FLOATING);	
	
      } else {
	lv_img_set_zoom(graph, LV_IMG_ZOOM_NONE);
	lv_obj_set_size(graph_cont, LV_PCT(100), LV_PCT(100));
	lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_FLOATING);
      }
      lv_obj_move_foreground(graph_cont);      
      image_fullsized = !image_fullsized;
    } 
  }
}

void BeltsCalibrationPanel::handle_macro_response(json &j) {
  spdlog::trace("belts calibration macro response: {}", j.dump());
  auto &v = j["/params/0"_json_pointer];
  if (v.is_null()) {
    return;
  }

  std::string resp = v.template get<std::string>();
  std::lock_guard<std::mutex> lock(lv_lock);

  if (run == RunState::idle) {
    return;
  }

  // Klipper broadcasts its errors with a !! prefix. A rejected TEST_RESONANCES,
  // an accelerometer that stopped answering and a shutdown mid sweep all arrive
  // this way, and none of them used to clear the spinner.
  if (resp.rfind("!! ", 0) == 0) {
    abandon_run(resp.substr(3));
    return;
  }

  // Klipper says when each capture has been written, which is the only signal
  // that a sweep has finished rather than merely stopped moving.
  if (run == RunState::testing_b &&
      resp == fmt::format("// Resonances data written to {} file", BELTS_CSV_B)) {
    start_test(RunState::testing_a, "1,-1", "A",
	       "Shaking belt A. This takes a few minutes.");
    return;
  }

  if (run == RunState::testing_a &&
      resp == fmt::format("// Resonances data written to {} file", BELTS_CSV_A)) {
    request_analysis();
    return;
  }

  if (resp.rfind("// Command {guppy_belts_calibration} timed out", 0) == 0) {
    abandon_run("Comparing the belts timed out. The K1 is short on memory, so "
		"this can be the plot rather than the maths.");
    return;
  }

  // graph_belts.py prints the similarity on its way to writing the plot, so a
  // run that got that far produced a result. gcode_shell_command reports
  // "finished" whenever the process exits, crash included, so "finished" on its
  // own says nothing about whether there is a plot to show.
  if (resp.find("Belts estimated similarity") != std::string::npos) {
    analysis_produced_result = true;
    return;
  }

  if (resp.rfind("// Command {guppy_belts_calibration} finished", 0) == 0) {
    if (!analysis_produced_result) {
      abandon_run("Comparing the belts produced no result. Check the Klipper log.");
      return;
    }

    auto config_root = KUtils::get_root_path("config");
    auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp" , BELTS_PNG);

    finish_run(fmt::format("A:{}", KUtils::is_running_local()
			   ? png_path
			   : KUtils::download_file("config", BELTS_PNG,
						   Config::get_instance()->get_thumbnail_path())));
  }
}

void BeltsCalibrationPanel::handle_update_slider(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    double hz = (double)lv_slider_get_value(excite_slider);
    lv_slider_set_value(excite_slider, hz, LV_ANIM_OFF);
    lv_label_set_text(excite_label, fmt::format("{} hz", hz / 10.0 ).c_str());
  }
}
