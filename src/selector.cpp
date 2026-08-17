#include "selector.h"
#include "spdlog/spdlog.h"

#include <limits>

namespace {
  bool is_row_break(const std::string &s) {
    return s == "\n";
  }
}

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   int32_t width_pct,
		   int32_t height_pct,
		   lv_event_cb_t cb,
		   void *cb_data)
  : cont(lv_obj_create(parent))
  , label(lv_label_create(cont))
  , btnm(lv_btnmatrix_create(cont))
  , selector_idx(default_idx)
{
  lv_obj_set_size(cont, LV_PCT(width_pct), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 0, 0);

  auto height = (double)lv_disp_get_physical_ver_res(NULL) * (height_pct / 100.0);
  height = height < 50 ? 50 : height;
  lv_obj_set_size(btnm, LV_PCT(100), height);
  lv_label_set_text(label, label_text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_set_style_pad_all(btnm, 4, LV_PART_MAIN);
  lv_obj_set_style_outline_width(btnm, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);

  lv_obj_add_event_cb(btnm, cb, LV_EVENT_VALUE_CHANGED, cb_data);

  // Copy the incoming literals into owned storage so there is a single code
  // path shared with the config driven set_options.
  std::vector<std::string> opts;
  for (const char *s : m) {
    if (s == NULL || s[0] == '\0') {
      continue;  // trailing terminator, apply_options adds its own
    }
    opts.push_back(s);
  }
  apply_options(opts, default_idx);
}

Selector::Selector(lv_obj_t *parent,
		   const char *label_text,
		   std::vector<const char*> m,
		   uint32_t default_idx,
		   lv_event_cb_t cb,
		   void *cb_data)
  : Selector(parent, label_text, m, default_idx, 62, 15, cb, cb_data)
{
}

Selector::~Selector() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void Selector::apply_options(const std::vector<std::string> &opts, uint32_t default_idx) {
  if (opts.empty()) {
    spdlog::error("selector given an empty option list, leaving it unchanged");
    return;
  }

  // Assign in one shot. Nothing may be appended after the pointers in map are
  // taken, since a reallocation would move short strings stored inline.
  owned = opts;
  owned.push_back("");

  map.clear();
  map.reserve(owned.size());
  for (const std::string &s : owned) {
    map.push_back(s.c_str());
  }

  lv_btnmatrix_set_map(btnm, &map[0]);

  // set_map reallocates the control bits and clears them, so the checkable
  // flags have to be reapplied every time the map changes.
  lv_btnmatrix_set_btn_ctrl_all(btnm, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(btnm, true);

  uint32_t count = option_count();
  if (default_idx != std::numeric_limits<uint32_t>::max()) {
    if (default_idx >= count) {
      spdlog::warn("selector default index {} is past the last of {} options, using 0",
		   default_idx, count);
      default_idx = 0;
    }
    selector_idx = default_idx;
    lv_btnmatrix_set_btn_ctrl(btnm, selector_idx, LV_BTNMATRIX_CTRL_CHECKED);
  } else {
    selector_idx = default_idx;
  }
}

void Selector::set_options(const std::vector<std::string> &opts, uint32_t default_idx) {
  apply_options(opts, default_idx);
}

void Selector::set_enabled(uint32_t idx, bool enabled) {
  if (idx >= option_count()) {
    return;
  }

  if (enabled) {
    lv_btnmatrix_clear_btn_ctrl(btnm, idx, LV_BTNMATRIX_CTRL_DISABLED);
  } else {
    lv_btnmatrix_set_btn_ctrl(btnm, idx, LV_BTNMATRIX_CTRL_DISABLED);
  }
}

uint32_t Selector::option_count() const {
  uint32_t count = 0;
  for (const std::string &s : owned) {
    if (s.empty()) {
      break;
    }
    if (!is_row_break(s)) {
      count++;
    }
  }
  return count;
}

const char *Selector::selected_text() {
  if (selector_idx == std::numeric_limits<uint32_t>::max() || selector_idx >= option_count()) {
    spdlog::error("selector has no valid selection (index {}, {} options)",
		  selector_idx, option_count());
    return nullptr;
  }

  const char *text = lv_btnmatrix_get_btn_text(btnm, selector_idx);
  if (text == NULL) {
    spdlog::error("selector index {} has no button text", selector_idx);
  }
  return text;
}

lv_obj_t *Selector::get_container() {
  return cont;
}

lv_obj_t *Selector::get_selector() {
  return btnm;
}

lv_obj_t *Selector::get_label() {
  return label;
}

uint32_t Selector::get_selected_idx() const {
  return selector_idx;
}

void Selector::set_selected_idx(uint32_t idx) {
  if (idx >= option_count()) {
    spdlog::warn("selector index {} is past the last of {} options, ignoring",
		 idx, option_count());
    return;
  }

  selector_idx = idx;

  // Move the check too. Every caller so far was recording a tap LVGL had
  // already drawn, so this was a no-op for them, but a caller selecting an
  // option in code got an index the widget did not show.
  // set_btn_ctrl clears the other buttons itself while one_checked is on.
  lv_btnmatrix_set_btn_ctrl(btnm, selector_idx, LV_BTNMATRIX_CTRL_CHECKED);
}
