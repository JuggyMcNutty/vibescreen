#include "inputshaper_panel.h"
#include "state.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>

LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(sd_img);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(back);

LV_FONT_DECLARE(dejavusans_mono_14);

#define X_DATA "/tmp/resonances_x_x.csv"
#define X_PNG "resonances_x.png"
#define Y_DATA "/tmp/resonances_y_y.csv"
#define Y_PNG "resonances_y.png"

// Every shaper Klipper accepts in [input_shaper], from
// k1/scripts/shaper_defs.py. Note this is six and not the five the analysis
// proposes: k1/scripts/shaper_calibrate.py leaves zvd out of AUTOTUNE_SHAPERS,
// so a calibration never suggests it, but Klipper's own SHAPER_CALIBRATE will
// and a config can be written by hand. The panel has to be able to show back
// what the printer is actually configured for.
std::vector<std::string> InputShaperPanel::shapers = {
  "zv",
  "mzv",
  "zvd",
  "ei",
  "2hump_ei",
  "3hump_ei"
};

namespace {
  // The axis row doubles as the calibration target, so "X + Y" is a selection
  // and not a separate control.
  const uint32_t TARGET_BOTH = 2;

  // configfile.config holds the raw text of printer.cfg, so every value in it
  // is a string whatever it looks like. Reading one must not throw:
  // foreground() runs inside the KGuard on PrinterTunePanel's button, so an
  // exception here means tapping Input Shaper does nothing at all and only
  // says so in the log.
  double config_number(const json &section, const char *key, double fallback) {
    auto v = section.find(key);
    if (v == section.end()) {
      return fallback;
    }
    if (v->is_string()) {
      return KUtils::parse_double(v->template get<std::string>(), fallback);
    }
    return v->is_number() ? v->template get<double>() : fallback;
  }

  std::string config_string(const json &section, const char *key) {
    auto v = section.find(key);
    return (v != section.end() && v->is_string())
      ? v->template get<std::string>()
      : std::string();
  }

  // Where the analysis writes its plot, and where we read it back from.
  std::string plot_directory() {
    auto config_root = KUtils::get_root_path("config");
    return config_root.length() > 0 ? config_root : "/tmp";
  }
}

InputShaperPanel::InputShaperPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , axis_sel(cont, "", {"X", "Y", "X + Y"}, TARGET_BOTH, 100, 11,
	     &InputShaperPanel::_handle_selector, this)
  , headline(lv_label_create(cont))
  , plot_cont(lv_obj_create(cont))
  , plot(lv_img_create(plot_cont))
  , table(lv_label_create(cont))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , view_sel(cont, "", {"Graph", "Numbers"}, 0, 100, 11,
	     &InputShaperPanel::_handle_selector, this)
  , status(lv_label_create(cont))
  // Two rows. Six of these across one row leaves about 50px a button, and
  // "2hump_ei" does not fit in that at any font this panel has.
  , shaper_sel(cont, "", {"zv", "mzv", "zvd", "\n", "ei", "2hump_ei", "3hump_ei"},
	       1, 100, 18, &InputShaperPanel::_handle_selector, this)
  , freq_cont(lv_obj_create(cont))
  , freq_slider(lv_slider_create(freq_cont))
  , freq_label(lv_label_create(freq_cont))
  , button_cont(lv_obj_create(cont))
  , calibrate_btn(button_cont, &resume, "Calibrate", &InputShaperPanel::_handle_callback, this)
  , save_btn(button_cont, &sd_img, "Save", &InputShaperPanel::_handle_callback, this)
  , emergency_btn(button_cont, &emergency, "Stop", &InputShaperPanel::_handle_callback, this,
		  "Do you want to emergency stop?",
		  [&c]() {
		    spdlog::debug("emergency stop pressed");
		    c.send_jsonrpc("printer.emergency_stop");
		  })
  , back_btn(button_cont, &back, "Back", &InputShaperPanel::_handle_callback, this)
  , axes({Axis{"X", X_DATA, X_PNG, false, RunState::idle, 0, true, 0.0, "", "", json()},
	  Axis{"Y", Y_DATA, Y_PNG, false, RunState::idle, 0, true, 0.0, "", "", json()}})
  , shown(0)
  , analysis_produced_result(false)
  , watchdog(NULL)
{
  lv_obj_move_background(cont);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;

  // The best result of the shown axis, so switching between the plot and the
  // numbers never hides the one line worth acting on.
  lv_label_set_long_mode(headline, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(headline, LV_TEXT_ALIGN_CENTER, 0);

  // The container stays in its cell at all times and only the image inside it
  // is hidden, so it is transparent. Hiding the container instead left it out
  // of the grid, and it kept LVGL's default 130px object size: the first plot
  // was then requested at 1.3 by 1.3 inches because that is what it measured.
  lv_obj_set_style_pad_all(plot_cont, 0, 0);
  lv_obj_set_style_bg_opa(plot_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(plot_cont, 0, 0);
  lv_obj_clear_flag(plot_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(plot);

  lv_obj_set_style_text_font(table, &dejavusans_mono_14, LV_STATE_DEFAULT);
  lv_label_set_text(table, "");

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(spinner, 90 * scale, 90 * scale);

  lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_clear_flag(freq_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(freq_cont, 0, 0);
  lv_obj_set_flex_flow(freq_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(freq_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			LV_FLEX_ALIGN_CENTER);

  lv_obj_set_width(freq_slider, LV_PCT(88));
  // Tenths of a Hz. The range is replaced in foreground() with what
  // [resonance_tester] says this printer can actually sweep.
  lv_slider_set_range(freq_slider, 50, 1400);
  lv_obj_add_event_cb(freq_slider, &InputShaperPanel::_handle_update_slider,
		      LV_EVENT_VALUE_CHANGED, this);
  lv_label_set_text(freq_label, "0.0 Hz");

  lv_obj_set_size(button_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(button_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(button_cont, 0, 0);
  lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
			LV_FLEX_ALIGN_CENTER);

  // Selectors are built with an empty caption. Hiding the label rather than
  // leaving it blank is what keeps it out of the container's flex column,
  // which otherwise adds a line of height to every row that holds one.
  for (Selector *sel : {&axis_sel, &view_sel, &shaper_sel}) {
    lv_obj_add_flag(sel->get_label(), LV_OBJ_FLAG_HIDDEN);
  }

  // Buttons down the left, then a header, the result, a view row and the
  // controls. The result gets the whole middle rather than a quarter of it.
  // Two content columns, so each row can put a control beside a label without
  // the two fighting over the same cell.
  static lv_coord_t grid_cols[] = {LV_GRID_FR(3), LV_GRID_FR(8), LV_GRID_FR(6),
    LV_GRID_TEMPLATE_LAST};
  // The last row carries two rows of shaper buttons, so it needs more than the
  // header and view rows that carry one.
  static lv_coord_t grid_rows[] = {LV_GRID_FR(2), LV_GRID_FR(8), LV_GRID_FR(2),
    LV_GRID_FR(4), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont, grid_cols, grid_rows);

  lv_obj_set_grid_cell(button_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 4);

  lv_obj_set_grid_cell(axis_sel.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
		       LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(headline, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  // Plot, numbers and spinner share the middle and take turns.
  lv_obj_set_grid_cell(plot_cont, LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(table, LV_GRID_ALIGN_CENTER, 1, 2, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(spinner, LV_GRID_ALIGN_CENTER, 1, 2, LV_GRID_ALIGN_CENTER, 1, 1);

  lv_obj_set_grid_cell(view_sel.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
		       LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(status, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);

  lv_obj_set_grid_cell(shaper_sel.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
		       LV_GRID_ALIGN_CENTER, 3, 1);
  lv_obj_set_grid_cell(freq_cont, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_CENTER, 3, 1);

  // Paused until a run starts, so an idle panel costs nothing.
  watchdog = lv_timer_create(&InputShaperPanel::_handle_watchdog, 1000, this);
  lv_timer_pause(watchdog);

  // TODO: show only register when issuing macros inputshaper cares about, then unregister after.
  ws.register_method_callback("notify_gcode_response",
			      "InputShaperPanel",
			      [this](json& d) { this->handle_macro_response(d); });

  // Klipper going away mid run is the one failure that produces no gcode
  // response at all, so the watchdog would be the only thing left to catch it
  // and that takes fifteen minutes.
  ws.register_method_callback("notify_klippy_disconnected",
			      "InputShaperPanel",
			      [this](json&) { this->handle_klippy_gone(); });
  ws.register_method_callback("notify_klippy_shutdown",
			      "InputShaperPanel",
			      [this](json&) { this->handle_klippy_gone(); });

  show_axis(0);
  set_status("");
}

InputShaperPanel::~InputShaperPanel() {
  if (watchdog != NULL) {
    lv_timer_del(watchdog);
    watchdog = NULL;
  }

  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

InputShaperPanel::Axis &InputShaperPanel::shown_axis() {
  return axes[shown];
}

bool InputShaperPanel::wants(size_t idx) const {
  uint32_t target = const_cast<Selector&>(axis_sel).get_selected_idx();
  return target == TARGET_BOTH || target == idx;
}

void InputShaperPanel::foreground() {
  auto inputshaper = State::get_instance()
    ->get_data("/printer_state/configfile/config/input_shaper"_json_pointer);
  spdlog::trace("input shaper {}", inputshaper.dump());

  if (inputshaper.is_object()) {
    axes[0].freq = config_number(inputshaper, "shaper_freq_x", 0.0);
    axes[1].freq = config_number(inputshaper, "shaper_freq_y", 0.0);

    // shaper_type is the fallback Klipper uses for an axis with no type of its
    // own, so it stands in when shaper_type_x or _y is absent.
    std::string fallback = config_string(inputshaper, "shaper_type");
    std::string types[] = {config_string(inputshaper, "shaper_type_x"),
			   config_string(inputshaper, "shaper_type_y")};

    for (size_t i = 0; i < axes.size(); i++) {
      std::string type = types[i].empty() ? fallback : types[i];
      if (!type.empty()) {
	axes[i].shaper = type;
	axes[i].shaper_known =
	  std::find(shapers.cbegin(), shapers.cend(), type) != shapers.cend();
	if (!axes[i].shaper_known) {
	  spdlog::warn("unknown input shaper '{}' on {}, leaving the selection alone",
		       type, axes[i].name);
	}
      }
    }
  }

  // Clamp the frequency control to what this printer will actually sweep,
  // the same rule LimitsPanel::init follows for velocity and acceleration.
  auto tester = State::get_instance()
    ->get_data("/printer_state/configfile/settings/resonance_tester"_json_pointer);
  if (tester.is_object()) {
    int lo = lround(config_number(tester, "min_freq", 5.0) * 10.0);
    int hi = lround(config_number(tester, "max_freq", 140.0) * 10.0);
    if (lo < hi) {
      lv_slider_set_range(freq_slider, lo, hi);
    }
  }

  show_axis(shown);
  update_available();
  lv_obj_move_foreground(cont);
}

void InputShaperPanel::show_axis(size_t idx) {
  shown = idx;
  Axis &axis = axes[idx];

  select_shaper(axis.shaper);
  set_frequency(axis.freq);

  lv_label_set_text(headline, render_headline(axis).c_str());
  lv_label_set_text(table, axis.result.is_null()
		    ? fmt::format("Calibrate to measure the {} axis.", axis.name).c_str()
		    : render_table(axis.result).c_str());

  if (axis.plot_path.empty()) {
    lv_img_set_src(plot, NULL);
    // hack to color in empty space.
    ((lv_img_t*)plot)->src_type = LV_IMG_SRC_SYMBOL;
  } else {
    lv_img_set_src(plot, axis.plot_path.c_str());
  }

  update_view();
}

void InputShaperPanel::update_view() {
  Axis &axis = shown_axis();
  bool running = axis.run != RunState::idle;
  bool want_plot = view_sel.get_selected_idx() == 0 && !axis.plot_path.empty();

  if (running) {
    lv_obj_add_flag(plot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(spinner);
    return;
  }

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

  if (want_plot) {
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(plot_cont);

    // The plot is drawn at the size of this cell, so it needs no scaling. The
    // guard is for one that was not: a stale file from a build with a
    // different panel, which would otherwise be clipped rather than shrunk.
    //
    // Sizes come from the image's own header and from the container after
    // forcing a layout. Reading the image as laid out geometry gives the
    // previous frame's numbers, which produced a plot a sixth of its size.
    lv_img_set_zoom(plot, LV_IMG_ZOOM_NONE);

    lv_coord_t iw = ((lv_img_t*)plot)->w;
    lv_coord_t ih = ((lv_img_t*)plot)->h;
    lv_obj_update_layout(cont);
    lv_coord_t cw = lv_obj_get_content_width(plot_cont);
    lv_coord_t ch = lv_obj_get_content_height(plot_cont);

    if (iw > cw || ih > ch) {
      lv_coord_t fit = std::min(LV_IMG_ZOOM_NONE * cw / iw, LV_IMG_ZOOM_NONE * ch / ih);
      lv_img_set_zoom(plot, std::max<lv_coord_t>(1, fit));
    }
    lv_obj_center(plot);
  } else {
    lv_obj_add_flag(plot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(table);
  }
}

std::string InputShaperPanel::render_headline(const Axis &axis) const {
  if (axis.result.is_null()) {
    return axis.freq > 0.0 && !axis.shaper.empty()
      ? fmt::format("{} axis is set to {} at {:.1f} Hz", axis.name, axis.shaper, axis.freq)
      : fmt::format("{} axis has not been calibrated", axis.name);
  }

  auto best = axis.result.find("best");
  auto entries = axis.result.find("shapers");
  if (best == axis.result.end() || entries == axis.result.end()) {
    return fmt::format("{} axis", axis.name);
  }

  auto name = best->template get<std::string>();
  auto entry = entries->find(name);
  if (entry == entries->end()) {
    return fmt::format("{} axis: best is {}", axis.name, name);
  }

  return fmt::format("{}: best is {} at {:.1f} Hz, {:.1f}% vibration, {:.0f} max accel",
		     axis.name, name,
		     (*entry)["freq"].template get<double>(),
		     (*entry)["vib"].template get<double>(),
		     (*entry)["max_acel"].template get<double>());
}

std::string InputShaperPanel::render_table(const json &result) const {
  auto entries = result.find("shapers");
  if (entries == result.end() || !entries->is_object()) {
    return "";
  }

  std::vector<std::string> rows;
  rows.push_back(fmt::format("{:<9}{:>7}{:>8}{:>8}{:>9}",
			     "Shaper", "Hz", "Vibr", "Smooth", "MaxAcc"));

  // In shaper_defs order rather than the alphabetical one a json object hands
  // back, so the list reads from least smoothing to most the way the shaper
  // definitions themselves are ordered.
  for (const std::string &name : shapers) {
    auto entry = entries->find(name);
    if (entry == entries->end()) {
      continue;
    }

    rows.push_back(fmt::format("{:<9}{:>7.1f}{:>7.1f}%{:>8.3f}{:>9.0f}",
			       name,
			       (*entry)["freq"].template get<double>(),
			       (*entry)["vib"].template get<double>(),
			       (*entry)["smooth"].template get<double>(),
			       (*entry)["max_acel"].template get<double>()));
  }

  return fmt::format("{}", fmt::join(rows, "\n"));
}

bool InputShaperPanel::select_shaper(const std::string &shaper) {
  auto it = std::find(shapers.cbegin(), shapers.cend(), shaper);
  if (it == shapers.cend()) {
    return false;
  }

  shaper_sel.set_selected_idx(std::distance(shapers.cbegin(), it));
  return true;
}

void InputShaperPanel::set_frequency(double hz) {
  // lround rather than a plain conversion, which truncated. The slider holds
  // tenths of a Hz and the label rounds to one decimal, so a calibration
  // returning 40.27 displayed 40.3 Hz and saved 40.2.
  lv_slider_set_value(freq_slider, lround(hz * 10.0), LV_ANIM_OFF);
  lv_label_set_text(freq_label,
		    fmt::format("{:.1f} Hz", lv_slider_get_value(freq_slider) / 10.0).c_str());
}

void InputShaperPanel::set_status(const std::string &text) {
  lv_label_set_text(status, text.c_str());
}

void InputShaperPanel::update_available() {
  // Klipper abandons the rest of a script at the first command it does not
  // like, so a button that sends something this printer lacks does worse than
  // nothing. Every one of these lives in configfile rather than in
  // printer.objects.list, see KUtils::has_config_section.
  bool can_test = KUtils::has_config_section("resonance_tester");
  bool can_analyse = KUtils::has_config_section("gcode_shell_command guppy_input_shaper");
  bool can_save = KUtils::has_config_section("calibrate_shaper_config");

  std::vector<std::string> missing;
  if (!can_test) {
    missing.push_back("[resonance_tester]");
  }
  if (!can_analyse) {
    missing.push_back("[gcode_shell_command guppy_input_shaper]");
  }
  if (!can_save) {
    missing.push_back("[calibrate_shaper_config]");
  }

  if (can_test && can_analyse) {
    calibrate_btn.enable();
  } else {
    calibrate_btn.disable();
  }

  bool types_known = axes[0].shaper_known && axes[1].shaper_known;
  if (can_save && types_known) {
    save_btn.enable();
  } else {
    save_btn.disable();
  }

  if (!missing.empty()) {
    // The install hint goes to the log rather than the status row, which has
    // three lines to work with and needs them for the section names.
    spdlog::warn("input shaper unavailable, printer config is missing {}. "
		 "GuppyScreen's guppy_cmd.cfg and k1_mods supply all but "
		 "resonance_tester", fmt::join(missing, ", "));
    set_status(fmt::format("Config is missing {}", fmt::join(missing, ", ")));
  } else if (!types_known) {
    set_status("Printer is set to a shaper this panel does not know. "
	       "Calibrate to replace it.");
  } else {
    set_status("");
  }
}

void InputShaperPanel::start_next_axis() {
  auto next = std::find_if(axes.begin(), axes.end(),
			   [](const Axis &a) { return a.pending; });
  if (next == axes.end()) {
    if (axes[0].run == RunState::idle && axes[1].run == RunState::idle) {
      set_status("");
    }
    return;
  }

  next->pending = false;
  size_t idx = std::distance(axes.begin(), next);

  // Home inside the same script as the test rather than as its own. Klipper
  // abandons the rest of a script at the first command it will not run, so a
  // failed G28 stops the test instead of shaking an unhomed toolhead.
  std::string script = KUtils::is_homed() ? "" : "G28\n";
  script += fmt::format("TEST_RESONANCES AXIS={} NAME={}\nM400",
			next->name, idx == 0 ? "x" : "y");
  ws.gcode_script(script);

  set_status(fmt::format("Testing {} resonances. This takes a few minutes.", next->name));
  start_run(idx);

  // Follow the axis being tested, so the spinner and the progress belong to
  // what the user is looking at.
  show_axis(idx);
}

void InputShaperPanel::start_run(size_t idx) {
  axes[idx].run = RunState::testing;
  axes[idx].since = lv_tick_get();
  lv_timer_resume(watchdog);
}

void InputShaperPanel::finish_run(size_t idx) {
  axes[idx].run = RunState::idle;
  if (axes[0].run == RunState::idle && axes[1].run == RunState::idle) {
    lv_timer_pause(watchdog);
  }
}

void InputShaperPanel::abandon_runs(const std::string &why, bool analysing_only) {
  // A shell command failing says nothing about an axis whose test has not run
  // yet, so those callers ask for the analysing axes only. An accelerometer
  // error or Klipper going away takes the whole queue with it, so those do not.
  auto doomed = [analysing_only](const Axis &a) {
    return a.run != RunState::idle && (!analysing_only || a.run == RunState::analysing);
  };

  if (!doomed(axes[0]) && !doomed(axes[1])) {
    return;
  }

  spdlog::warn("abandoning input shaper runs, {}", why);

  // Whatever stopped this axis, an accelerometer or a missing script, stops
  // the next one too. Sending it anyway would shake the machine for another
  // two minutes to reach the same failure.
  for (Axis &axis : axes) {
    axis.pending = false;
    if (doomed(axis)) {
      axis.run = RunState::idle;
    }
  }

  lv_timer_pause(watchdog);
  set_status(why);
  update_view();
}

void InputShaperPanel::check_timeouts() {
  // Only fires when nothing at all is coming back. A run on the development
  // printer takes about 128 s per axis at its configured hz_per_sec of 1.0,
  // and gcode_shell_command's own timeout is 600 s, so anything past this has
  // stopped rather than slowed down.
  const uint32_t limit = 15 * 60 * 1000;

  bool stalled = std::any_of(axes.begin(), axes.end(), [limit](const Axis &a) {
    return a.run != RunState::idle && lv_tick_elaps(a.since) > limit;
  });

  if (stalled) {
    abandon_runs("The printer stopped reporting on the resonance test. "
		 "Check the Klipper log.");
  }
}

void InputShaperPanel::request_analysis(Axis &axis) {
  auto png_path = fmt::format("{}/{}", plot_directory(), axis.png);

  // Ask matplotlib for exactly the space the plot gets, at its default 100 dpi.
  // Drawing at screen size and shrinking it to fit, which is what this did,
  // shrinks the axis labels and the legend with it until they are unreadable;
  // drawing at the target size lays them out for it instead.
  lv_obj_update_layout(cont);
  lv_coord_t cw = lv_obj_get_content_width(plot_cont);
  lv_coord_t ch = lv_obj_get_content_height(plot_cont);
  if (cw <= 0 || ch <= 0) {
    cw = lv_disp_get_physical_hor_res(NULL);
    ch = lv_disp_get_physical_ver_res(NULL);
  }

  // Always ask for the plot. It used to be tied to the Graph switch, so
  // flipping the view after a run meant re-running the whole test to get the
  // picture, and the analysis is the expensive half either way.
  std::string arg = fmt::format("{} -o {} -w {:.2f} -l {:.2f}",
				axis.csv, png_path, cw / 100.0, ch / 100.0);

  axis.run = RunState::analysing;
  axis.since = lv_tick_get();
  set_status(fmt::format("Analysing {} resonance data.", axis.name));
  ws.gcode_script(fmt::format("RUN_SHELL_COMMAND CMD=guppy_input_shaper PARAMS={:?}", arg));
}

void InputShaperPanel::apply_result(Axis &axis, json &res) {
  axis.result = res;

  auto best = res.find("best");
  auto entries = res.find("shapers");
  if (best != res.end() && entries != res.end()) {
    auto name = best->template get<std::string>();
    auto entry = entries->find(name);
    if (entry != entries->end()) {
      axis.shaper = name;
      axis.shaper_known = select_shaper(name);
      axis.freq = (*entry)["freq"].template get<double>();
    }
  }

  auto png = res.find("png");
  if (png != res.end() && png->is_string()) {
    auto local = KUtils::is_running_local()
      ? png->template get<std::string>()
      : KUtils::download_file("config", axis.png,
			      Config::get_instance()->get_thumbnail_path());
    axis.plot_path = fmt::format("A:{}", local);
  }
}

void InputShaperPanel::handle_macro_response(json &j) {
  spdlog::trace("inputshaper macro response: {}", j.dump());
  auto &v = j["/params/0"_json_pointer];
  if (v.is_null()) {
    return;
  }

  std::string resp = v.template get<std::string>();
  std::lock_guard<std::mutex> lock(lv_lock);

  // Nothing used to clear a spinner except success, so an accelerometer that
  // stopped answering, or an analysis that never printed its payload, left the
  // panel waiting on something that was never coming. These are the ways a run
  // really ends badly, taken from k1/k1_mods/gcode_shell_command.py and from
  // Klipper broadcasting its errors with a !! prefix.
  if (resp.rfind("!! ", 0) == 0) {
    abandon_runs(resp.substr(3));
    return;
  }

  // Klipper runs shell commands one at a time, so these lines bracket exactly
  // one analysis. Judging "finished" by whether a payload arrived inside the
  // bracket is what keeps one axis finishing from cancelling the other's.
  if (resp.rfind("// Running Command {guppy_input_shaper}", 0) == 0) {
    analysis_produced_result = false;
    return;
  }

  if (resp.rfind("// Command {guppy_input_shaper} timed out", 0) == 0) {
    abandon_runs("Analysing the resonance data timed out. The K1 is short on "
		 "memory, so this can be the plot rather than the maths.", true);
    return;
  }

  if (resp.rfind("// Command {guppy_input_shaper} finished", 0) == 0) {
    if (!analysis_produced_result) {
      abandon_runs("Analysing the resonance data produced no result. "
		   "Check the Klipper log.", true);
    }
    return;
  }

  if (resp.rfind("// {\"shapers\":", 0) == 0) {
    analysis_produced_result = true;

    auto res = json::parse(resp.substr(3));
    auto logfile = res.find("logfile");
    if (logfile == res.end() || !logfile->is_string()) {
      return;
    }

    std::string fn = logfile->template get<std::string>();
    auto it = std::find_if(axes.begin(), axes.end(), [&fn](const Axis &a) {
      return fn == a.csv;
    });

    // A result for an axis this panel has stopped waiting on belongs to a run
    // that was already abandoned.
    if (it == axes.end() || it->run != RunState::analysing) {
      return;
    }

    apply_result(*it, res);
    finish_run(std::distance(axes.begin(), it));
    show_axis(std::distance(axes.begin(), it));
    update_available();
    start_next_axis();
    return;
  }

  for (Axis &axis : axes) {
    if (resp == fmt::format("// Resonances data written to {} file", axis.csv)) {
      // Only when this panel is still waiting on it. The printer carries on
      // through a run the panel has already given up on, and acting on those
      // late messages would send another analysis for a result the user has
      // been told failed.
      if (axis.run == RunState::testing) {
	request_analysis(axis);
      }
      return;
    }
  }
}

void InputShaperPanel::handle_klippy_gone() {
  std::lock_guard<std::mutex> lock(lv_lock);
  abandon_runs("Klipper disconnected while the resonance test was running.");
}

void InputShaperPanel::handle_selector(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  lv_obj_t *selector = lv_event_get_target(e);
  uint32_t idx = lv_btnmatrix_get_selected_btn(selector);

  if (selector == axis_sel.get_selector()) {
    axis_sel.set_selected_idx(idx);
    // "X + Y" is a calibration target rather than a view, so it leaves the
    // shown axis where it was.
    if (idx != TARGET_BOTH) {
      show_axis(idx);
    }

  } else if (selector == view_sel.get_selector()) {
    view_sel.set_selected_idx(idx);
    update_view();

  } else if (selector == shaper_sel.get_selector()) {
    shaper_sel.set_selected_idx(idx);
    if (idx < shapers.size()) {
      Axis &axis = shown_axis();
      axis.shaper = shapers[idx];
      axis.shaper_known = true;
      update_available();
    }
  }
}

void InputShaperPanel::handle_update_slider(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  double hz = (double)lv_slider_get_value(freq_slider) / 10.0;
  lv_label_set_text(freq_label, fmt::format("{:.1f} Hz", hz).c_str());
  shown_axis().freq = hz;
}

void InputShaperPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);

  if (btn == calibrate_btn.get_container()) {
    if (axes[0].run != RunState::idle || axes[1].run != RunState::idle) {
      return;
    }

    for (size_t i = 0; i < axes.size(); i++) {
      axes[i].pending = wants(i);
      if (axes[i].pending) {
	axes[i].result = json();
	axes[i].plot_path.clear();
      }
    }

    start_next_axis();

  } else if (btn == save_btn.get_container()) {
    // Both axes every time. SAVE_INPUT_SHAPER takes its per axis defaults from
    // [calibrate_shaper_config], not from the running [input_shaper], so
    // sending only one axis would write the module's own default over the
    // other one. Measured on the K1 Max: that default is mzv at 0.0 Hz, which
    // disables shaping on the axis it lands on.
    ws.gcode_script(
      fmt::format("SAVE_INPUT_SHAPER SHAPER_FREQ_X={:.1f} SHAPER_TYPE_X={} "
		  "SHAPER_FREQ_Y={:.1f} SHAPER_TYPE_Y={}\nSAVE_CONFIG",
		  axes[0].freq, axes[0].shaper, axes[1].freq, axes[1].shaper));

  } else if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);

  } else if (btn == emergency_btn.get_container()) {
    ws.send_jsonrpc("printer.emergency_stop");
  }
}
