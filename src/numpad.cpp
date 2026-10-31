#include "numpad.h"
#include "widget_handle.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <cmath>
#include <limits>
#include <string>

Numpad::Numpad(lv_obj_t *parent, bool decimal)
  : edit_cont(lv_obj_create(parent))
  , input(lv_textarea_create(edit_cont))
  , kb(lv_keyboard_create(edit_cont))
  , ready_cb([](double v){})
  , min_value(std::numeric_limits<double>::lowest())
  , max_value(std::numeric_limits<double>::max())
{
  KWidget::null_on_delete(&edit_cont);
  spdlog::trace("creating numpad on main_cont");
  lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_clear_flag(edit_cont, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_move_background(edit_cont);
  lv_obj_set_size(edit_cont, LV_PCT(48), LV_PCT(100));

  lv_obj_set_flex_align(edit_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(edit_cont, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_set_flex_flow(edit_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(edit_cont, 0, 0);

  lv_obj_set_size(input, LV_PCT(100), LV_SIZE_CONTENT);
  lv_textarea_set_one_line(input, true);

  lv_obj_set_size(kb, LV_PCT(100), LV_PCT(85));
  static const char * kb_map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, NULL };
  static const lv_btnmatrix_ctrl_t kb_ctrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);

  // A mode of its own, because lv_keyboard_set_map keeps one map per mode in a
  // table shared by every keyboard. Registering this under USER_1 as well would
  // change the keys on the temperature numpad. LVGL's own handler toggles a
  // leading minus for "+/-".
  static const char * kb_map_decimal[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
    "+/-", "0", ".", "\n", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, NULL };
  static const lv_btnmatrix_ctrl_t kb_ctrl_decimal[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_2, kb_map_decimal, kb_ctrl_decimal);

  lv_keyboard_set_mode(kb, decimal ? LV_KEYBOARD_MODE_USER_2 : LV_KEYBOARD_MODE_USER_1);
  lv_keyboard_set_textarea(kb, input);

  lv_obj_add_event_cb(input, &Numpad::_handle_input, LV_EVENT_ALL, this);
}

Numpad::~Numpad() {
  if (edit_cont != NULL) {
    lv_obj_del(edit_cont);
    edit_cont = NULL;
  }
}

void Numpad::set_callback(std::function<void(double)> cb) {
  ready_cb = cb;
}

void Numpad::set_range(double min, double max) {
  min_value = min;
  max_value = max;
  lv_textarea_set_placeholder_text(input, fmt::format("{:g} to {:g}", min, max).c_str());
}

void Numpad::handle_input(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(edit_cont);
  }

  if (code == LV_EVENT_READY) {
    std::string value = std::string(lv_textarea_get_text(input));
    if (value.length() > 0) {
      double v = KUtils::parse_double(value, std::numeric_limits<double>::quiet_NaN());
      // Refused rather than passed on: a bare sign or point, which the decimal
      // keys can produce, or anything outside the range. Clearing the text
      // instead of closing puts the range back up as the placeholder and leaves
      // the pad open for another go.
      if (std::isnan(v) || v < min_value || v > max_value) {
	lv_textarea_set_text(input, "");
	return;
      }
      ready_cb(v);
    }

    lv_obj_add_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(edit_cont);
    lv_textarea_set_text(input, "");
  }
}

void Numpad::foreground_reset() {
  spdlog::trace("resetting foreground");
  lv_textarea_set_text(input, "");
  lv_obj_clear_flag(edit_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(edit_cont);
}
