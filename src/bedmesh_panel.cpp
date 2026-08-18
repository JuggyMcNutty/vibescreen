#include "bedmesh_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(delete_img);
LV_IMG_DECLARE(bedmesh_img);
LV_IMG_DECLARE(sd_img);

namespace {
  // The mesh view is a fixed size pixel buffer rather than a widget that
  // reflows, so its dimensions are worked out up front from the display, the
  // same way every other size in this panel is.
  lv_coord_t mesh_canvas_width() {
    return (lv_coord_t)(456.0 * lv_disp_get_physical_hor_res(NULL) / 800.0);
  }

  lv_coord_t mesh_canvas_height() {
    return (lv_coord_t)(252.0 * lv_disp_get_physical_ver_res(NULL) / 480.0);
  }

  std::vector<std::vector<double>> matrix_from(const json &j) {
    if (j.is_null() || !j.is_array()) {
      return {};
    }
    return j.template get<std::vector<std::vector<double>>>();
  }

  // mesh_min and mesh_max are [x, y] in millimetres. Off the websocket, so
  // check the shape rather than trusting it.
  bool corner_from(const json &j, double &x, double &y) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
      return false;
    }
    x = j[0].template get<double>();
    y = j[1].template get<double>();
    return true;
  }
}

BedMeshPanel::BedMeshPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , prompt(lv_obj_create(lv_scr_act()))
  , top_cont(lv_obj_create(cont))
  , mesh_cont(lv_obj_create(top_cont))
  , mesh_view(mesh_cont, mesh_canvas_width(), mesh_canvas_height())
  , stats_label(lv_label_create(mesh_cont))
  , selector_cont(lv_obj_create(mesh_cont))
  , view_selector(selector_cont, "View", {"3D", "Flat"}, 0, 48, 8,
		  &BedMeshPanel::_handle_selector, this)
  , matrix_selector(selector_cont, "Matrix", {"Probed", "Interp"}, 1, 48, 8,
		    &BedMeshPanel::_handle_selector, this)
  , profile_cont(lv_obj_create(top_cont))
  , profile_table(lv_table_create(profile_cont))
  , profile_info(lv_table_create(profile_cont))
  , controls_cont(lv_obj_create(cont))
  , save_btn(controls_cont, &sd_img, "Save Profile", &BedMeshPanel::_handle_callback, this)
  , clear_btn(controls_cont, &delete_img, "Clear Profile", &BedMeshPanel::_handle_callback, this)
  , calibrate_btn(controls_cont, &bedmesh_img, "Calibrate", &BedMeshPanel::_handle_callback, this)
  , back_btn(controls_cont, &back, "Back", &BedMeshPanel::_handle_callback, this)
  , msgbox(lv_obj_create(prompt))
  , input(lv_textarea_create(msgbox))
  , kb(lv_keyboard_create(prompt))
{
  lv_obj_move_background(cont);

  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 0, 0);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(top_cont, 1);

  lv_obj_set_flex_align(top_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_set_style_pad_all(top_cont, 2, 0);
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);

  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  auto scale = (double)screen_width / 800.0;
  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

  // mesh side
  lv_obj_set_flex_grow(mesh_cont, 1);
  lv_obj_set_height(mesh_cont, LV_PCT(100));
  lv_obj_set_style_pad_all(mesh_cont, 0, 0);
  lv_obj_set_style_pad_row(mesh_cont, 2, 0);
  lv_obj_set_style_border_width(mesh_cont, 0, 0);
  lv_obj_clear_flag(mesh_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(mesh_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mesh_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			LV_FLEX_ALIGN_CENTER);

  lv_obj_set_width(stats_label, LV_PCT(100));
  lv_obj_set_style_text_font(stats_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(stats_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(stats_label, "");

  lv_obj_set_size(selector_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(selector_cont, 0, 0);
  lv_obj_set_style_border_width(selector_cont, 0, 0);
  lv_obj_clear_flag(selector_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(selector_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(selector_cont, LV_FLEX_ALIGN_SPACE_EVENLY,
			LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (Selector *s : {&view_selector, &matrix_selector}) {
    lv_obj_set_style_text_font(s->get_label(), &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s->get_selector(), &lv_font_montserrat_14, LV_STATE_DEFAULT);
  }

  // profile side
  lv_obj_set_size(profile_cont, LV_PCT(40), 340 * hscale);
  lv_obj_set_style_pad_all(profile_cont, 0, 0);
  lv_obj_set_style_border_width(profile_cont, 0, 0);
  lv_obj_clear_flag(profile_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(profile_cont, LV_FLEX_FLOW_COLUMN);

  // profile table
  lv_table_set_col_width(profile_table, 0, 200 * scale);
  lv_table_set_col_width(profile_table, 1, 50 * scale);
  lv_table_set_col_width(profile_table, 2, 50 * scale);
  lv_obj_set_height(profile_table, 190 * hscale);

  // profile info. The X and Y rows carry four numbers, two of them fractional
  // millimetres, so the value column needs most of the width.
  lv_table_set_col_width(profile_info, 0, 150 * scale);
  lv_table_set_col_width(profile_info, 1, 160 * scale);
  lv_obj_set_height(profile_info, 150 * hscale);
  lv_obj_set_style_text_font(profile_info, &lv_font_montserrat_12, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(profile_info, 5, LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(profile_info, 5, LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_set_style_border_side(profile_info, LV_BORDER_SIDE_BOTTOM, 0);

  // button controls
  lv_obj_set_width(controls_cont, LV_PCT(100));
  lv_obj_set_flex_align(controls_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_flow(controls_cont, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(controls_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_event_cb(profile_table, &BedMeshPanel::_handle_profile_action, LV_EVENT_VALUE_CHANGED, this);

  // prompt
  lv_obj_set_style_pad_all(prompt, 0, 0);
  lv_obj_set_size(prompt, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);  
  lv_obj_set_style_bg_opa(prompt, LV_OPA_70, 0);

  lv_textarea_set_one_line(input, true);
  lv_obj_set_width(input, LV_PCT(100));
  
  // lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(msgbox, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(msgbox, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(msgbox, 25, 0);
  
  lv_obj_clear_flag(msgbox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(msgbox, LV_PCT(60), LV_PCT(40));
  lv_obj_set_style_border_width(msgbox, 2, 0);
  lv_obj_set_style_bg_color(msgbox, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  lv_obj_align(msgbox, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t * label = NULL;

  label = lv_label_create(msgbox);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_text(label, "Saving the profile will restart the printer.");

  lv_obj_t *prompt_save_btn = lv_btn_create(msgbox);
  lv_obj_t *prompt_cancel_btn = lv_btn_create(msgbox);

  label = lv_label_create(prompt_save_btn);
  lv_label_set_text(label, "Save");

  label = lv_label_create(prompt_cancel_btn);
  lv_label_set_text(label, "Cancel");
  lv_obj_center(label);

  lv_obj_add_event_cb(prompt_save_btn, &BedMeshPanel::_handle_prompt_save, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(prompt_cancel_btn, &BedMeshPanel::_handle_prompt_cancel, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(input, &BedMeshPanel::_handle_kb_input, LV_EVENT_ALL, this);
  lv_keyboard_set_textarea(kb, input);

  lv_obj_move_background(prompt);

  ws.register_notify_update(this);
}

BedMeshPanel::~BedMeshPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }

  if (prompt != NULL) {
    lv_obj_del(prompt);
    prompt = NULL;
  }
}

void BedMeshPanel::consume(json &j) {
  auto bm = j["/params/0/bed_mesh"_json_pointer];
  if (!bm.is_null()) {
    spdlog::trace("bedmesh panel consume {}",  bm["/profiles"_json_pointer].dump());
    refresh_views_with_lock(bm);
  }
}

void BedMeshPanel::refresh_views_with_lock(json &bm) {
  std::lock_guard<std::mutex> lock(lv_lock); // more grandular?
  refresh_views(bm);
}

void BedMeshPanel::refresh_views(json &bm) {
  if (bm.is_null()) {
    return;
  }

  State *state = State::get_instance();

  // A subscription update only carries the keys that changed, so any of these
  // can be absent from a perfectly valid delta. Falling back to the cached
  // copy is what keeps a matrices-only update, which is what arrives during a
  // calibrate, from being read as "there is no profile".
  auto active_profile_j = bm["/profile_name"_json_pointer];
  if (active_profile_j.is_null()) {
    active_profile_j = state->get_data("/printer_state/bed_mesh/profile_name"_json_pointer);
  }
  active_profile = active_profile_j.is_null()
    ? "" : active_profile_j.template get<std::string>();

  if (active_profile.length() > 0) {
    save_btn.enable();
    refresh_profile_info(active_profile);

    auto probed_j = bm["/probed_matrix"_json_pointer];
    if (probed_j.is_null()) {
      probed_j = state->get_data("/printer_state/bed_mesh/probed_matrix"_json_pointer);
    }
    probed = matrix_from(probed_j);

    auto interp_j = bm["/mesh_matrix"_json_pointer];
    if (interp_j.is_null()) {
      interp_j = state->get_data("/printer_state/bed_mesh/mesh_matrix"_json_pointer);
    }
    interpolated = matrix_from(interp_j);

    // The bed area the loaded mesh covers, which labels the corners of the 3D
    // view. This and not the active profile's mesh_params below: an adaptive
    // calibrate loads a mesh over the objects being printed, which is a
    // smaller area than the profile it was saved from.
    auto mesh_min_j = bm["/mesh_min"_json_pointer];
    if (mesh_min_j.is_null()) {
      mesh_min_j = state->get_data("/printer_state/bed_mesh/mesh_min"_json_pointer);
    }
    auto mesh_max_j = bm["/mesh_max"_json_pointer];
    if (mesh_max_j.is_null()) {
      mesh_max_j = state->get_data("/printer_state/bed_mesh/mesh_max"_json_pointer);
    }

    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
    if (!corner_from(mesh_min_j, min_x, min_y) || !corner_from(mesh_max_j, max_x, max_y)) {
      // Zeroes mark the extents unknown, so the last profile's numbers cannot
      // stay on a mesh they do not describe.
      min_x = min_y = max_x = max_y = 0.0;
    }
    mesh_view.set_extents(min_x, min_y, max_x, max_y);

    auto algo = state->get_data(json::json_pointer(
        fmt::format("/printer_state/bed_mesh/profiles/{}/mesh_params/algo", active_profile)));
    mesh_summary = active_profile;
    if (!probed.empty() && !probed[0].empty()) {
      mesh_summary += fmt::format("   {}x{} probed", probed[0].size(), probed.size());
    }
    if (!algo.is_null()) {
      mesh_summary += fmt::format("   {}", algo.template get<std::string>());
    }

    show_selected_matrix();
  } else {
    // No active profile. BED_MESH_CLEAR lands here, and leaving the last
    // render up would claim a mesh is loaded when none is.
    probed.clear();
    interpolated.clear();
    mesh_summary.clear();
    mesh_view.clear();
    lv_label_set_text(stats_label, "No mesh loaded");
    save_btn.disable();
  }

  // populate profiles tables
  auto profiles = bm["/profiles"_json_pointer];
  if (profiles.is_null()) {
    profiles = state->get_data("/printer_state/bed_mesh/profiles"_json_pointer);
  }

  spdlog::trace("active {}, profiles is {}", active_profile, profiles.dump());

  if (profiles.size() > 0) {
    lv_obj_clear_flag(profile_cont, LV_OBJ_FLAG_HIDDEN);

    size_t row_idx = 0;
    for (auto &el : profiles.items()) {
      lv_table_clear_cell_ctrl(profile_table, row_idx, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);

      bool is_active = el.key() == active_profile;
      if (is_active) {
	lv_table_add_cell_ctrl(profile_table, row_idx, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
	lv_table_set_cell_value(profile_table, row_idx, 1, "");
      } else {
	lv_table_set_cell_value(profile_table, row_idx, 1, LV_SYMBOL_UPLOAD);
      }

      lv_table_set_cell_value(profile_table, row_idx, 0, el.key().c_str());
      lv_table_set_cell_value(profile_table, row_idx, 2, LV_SYMBOL_CLOSE);
      row_idx++;
    }
    lv_table_set_row_cnt(profile_table, row_idx);
  } else {
    // no profiles, hide profile table
    lv_obj_add_flag(profile_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void BedMeshPanel::show_selected_matrix() {
  const bool want_interpolated = matrix_selector.get_selected_idx() == 1;
  // Klipper omits mesh_matrix on some versions and profiles, so the
  // interpolated view has to be able to fall back to what was probed.
  const std::vector<std::vector<double>> &m =
    (want_interpolated && !interpolated.empty()) ? interpolated : probed;

  mesh_view.set_mesh(m);

  if (!mesh_view.has_mesh()) {
    lv_label_set_text(stats_label, mesh_summary.empty()
		      ? "No mesh loaded" : mesh_summary.c_str());
    return;
  }

  lv_label_set_text(stats_label,
		    fmt::format("{}\nRange {:.3f}   Min {:+.3f}   Max {:+.3f}",
				mesh_summary, mesh_view.max_z() - mesh_view.min_z(),
				mesh_view.min_z(), mesh_view.max_z()).c_str());
}

void BedMeshPanel::refresh_profile_info(std::string profile) {
  auto mesh_params = State::get_instance()->get_data(json::json_pointer(
			fmt::format("/printer_state/bed_mesh/profiles/{}/mesh_params", profile)));
  spdlog::trace("refreshing profile info {}, {}", profile, mesh_params.dump());

  if (!mesh_params.is_null()) {
    uint32_t rowidx = 0;
    auto &v = mesh_params["/algo"_json_pointer];
    if (!v.is_null()) {
      lv_table_set_cell_value(profile_info, rowidx, 0, "Algorithm");
      lv_table_set_cell_value(profile_info, rowidx, 1, v.template get<std::string>().c_str());
      rowidx++;
    }

    v = mesh_params["/tension"_json_pointer];
    if (!v.is_null()) {
      lv_table_set_cell_value(profile_info, rowidx, 0, "Tension");
      lv_table_set_cell_value(profile_info, rowidx, 1, fmt::format("{}", v.template get<double>()).c_str());
      rowidx++;
    }

    // min and max are millimetres and can be fractional. Reading them as int,
    // which nlohmann truncates rather than rejecting, turned 190.19 into 190.
    auto axis_row = [&](const char *label, const char *mn, const char *mx,
			const char *count, const char *pps) {
      std::vector<std::string> values;
      for (const char *param : {mn, mx}) {
	auto &pv = mesh_params[json::json_pointer(fmt::format("/{}", param))];
	if (!pv.is_null()) {
	  values.push_back(fmt::format("{:g}", pv.template get<double>()));
	}
      }
      for (const char *param : {count, pps}) {
	auto &pv = mesh_params[json::json_pointer(fmt::format("/{}", param))];
	if (!pv.is_null()) {
	  values.push_back(fmt::format("{}", pv.template get<int>()));
	}
      }
      lv_table_set_cell_value(profile_info, rowidx, 0, label);
      lv_table_set_cell_value(profile_info, rowidx, 1,
			      fmt::format("{}", fmt::join(values, ", ")).c_str());
      rowidx++;
    };

    axis_row("X min/max/n/pps", "min_x", "max_x", "x_count", "mesh_x_pps");
    axis_row("Y min/max/n/pps", "min_y", "max_y", "y_count", "mesh_y_pps");

    // A profile with fewer parameters than the last one would otherwise leave
    // the rows it did not write showing the previous profile's values.
    lv_table_set_row_cnt(profile_info, rowidx);
  }
}

void BedMeshPanel::foreground() {
  auto bm = State::get_instance()->get_data("/printer_state/bed_mesh"_json_pointer);
  spdlog::trace("bm {}", bm.dump());
  refresh_views(bm);
  
  lv_obj_move_foreground(cont);
}

void BedMeshPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == save_btn.get_container()) {
    spdlog::trace("mesh save pressed");
    lv_obj_clear_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);

    if (active_profile.length() > 0) {
      lv_textarea_set_text(input, active_profile.c_str());
    }
    
    lv_obj_move_foreground(prompt);
    
  } else if (btn == clear_btn.get_container()) {
    spdlog::trace("mesh clear pressed");
    ws.gcode_script("BED_MESH_CLEAR");
    
  } else if (btn == calibrate_btn.get_container()) {
    spdlog::trace("mesh calibrate pressed");
    std::string script;

    // Probing needs all three axes homed, Z included, because the probe lifts
    // to horizontal_move_z before it starts.
    if (!KUtils::is_homed()) {
      script += "G28\n";
    }

    // A blob of filament on the nozzle is measured as bed, so wipe first on
    // printers that can. WIPE_NOZZLE comes from the ProWiper mod, formerly
    // Advanced Nozzle Wiper, and is not on a stock machine, so it has to be
    // checked for rather than sent hopefully: Klipper abandons the rest of a
    // script at the first unknown command, which would mean Calibrate popped an
    // error and never probed at all for anyone without the mod.
    // https://www.printables.com/model/1023575-prowiper-for-creality-k1-series
    //
    // It is its own no-op when the wiper is toggled off, and homes itself only
    // when it has to, so the decision above still governs. Wrapped because it
    // leaves G90 set without putting it back, and a modal leak corrupts a
    // print that is merely paused.
    if (KUtils::has_gcode_macro("WIPE_NOZZLE")) {
      script += "SAVE_GCODE_STATE NAME=guppy_bedmesh_wipe\n"
	"WIPE_NOZZLE\n"
	"RESTORE_GCODE_STATE NAME=guppy_bedmesh_wipe\n";
    }

    script += "BED_MESH_CALIBRATE";
    ws.gcode_script(script);

  } else if (btn == back_btn.get_container()) {
    spdlog::trace("back button pressed");
    lv_obj_move_background(cont);
  }
}

void BedMeshPanel::handle_profile_action(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(profile_table, &row, &col);
    uint16_t row_count = lv_table_get_row_cnt(profile_table);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE || row >= row_count) {
      return;
    }
    const char *selected = lv_table_get_cell_value(profile_table, row, col);    
    const char *profile_name = lv_table_get_cell_value(profile_table, row, 0);
    spdlog::trace("selected {}, {}, value {}", row, col, profile_name);
    if (col == 2) {
      // delete profile
      spdlog::trace("delete mesh {}", profile_name);
      ws.gcode_script(fmt::format("BED_MESH_PROFILE REMOVE=\"{}\"\nSAVE_CONFIG", profile_name));
    } else if (col == 1 && selected != NULL && strlen(selected) != 0) {
      // load profile
      spdlog::trace("selected {}, load mesh {}", strlen(selected), profile_name);
      ws.gcode_script(fmt::format("BED_MESH_PROFILE LOAD=\"{}\"", profile_name));

      // // populate profile info
      // refresh_profile_info(profile_name);
    } else if (col == 0) {
      // display mesh info and refresh bed mesh matrix
    }
      
  }
}

void BedMeshPanel::handle_prompt_save(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    const char *profile_name = lv_textarea_get_text(input);
    if (profile_name == NULL || strlen(profile_name) == 0) {
      return;
    }

    ws.gcode_script(fmt::format("BED_MESH_PROFILE SAVE=\"{}\"\nSAVE_CONFIG", profile_name));

    lv_textarea_set_text(input, "");
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);  
    lv_obj_move_background(prompt);
  }  
}

void BedMeshPanel::handle_prompt_cancel(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);  
    lv_obj_move_background(prompt);
  }
}


void BedMeshPanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char *profile_name = lv_textarea_get_text(input);
    if (profile_name == NULL || strlen(profile_name) == 0) {
      return;
    }

    ws.gcode_script(fmt::format("BED_MESH_PROFILE SAVE=\"{}\"\nSAVE_CONFIG", profile_name));

    lv_textarea_set_text(input, "");
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);  
    lv_obj_move_background(prompt);
  }
}

void BedMeshPanel::handle_selector(lv_event_t *e)
{
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }

  lv_obj_t *selector = lv_event_get_target(e);
  uint32_t idx = lv_btnmatrix_get_selected_btn(selector);

  if (selector == view_selector.get_selector()) {
    view_selector.set_selected_idx(idx);
    mesh_view.set_mode(idx == 0 ? MeshView::Mode::Surface : MeshView::Mode::Flat);
  } else if (selector == matrix_selector.get_selector()) {
    matrix_selector.set_selected_idx(idx);
    show_selected_matrix();
  }
}
