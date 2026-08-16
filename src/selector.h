#ifndef __K_SELECTOR_H__
#define __K_SELECTOR_H__

#include "lvgl/lvgl.h"
#include <string>
#include <vector>

// A labelled row of mutually exclusive buttons backed by an lv_btnmatrix.
//
// The option list may contain "\n" entries to break the buttons across
// multiple rows. LVGL treats those purely as row separators and excludes them
// from button indexing, so every index used here stays dense and 0 based
// regardless of how many rows there are.
class Selector {
 public:
  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   int32_t width_pct,
	   int32_t height_pct,
	   lv_event_cb_t cb,
	   void *cb_data);

  Selector(lv_obj_t *parent,
	   const char *label_text,
	   std::vector<const char*> map,
	   uint32_t default_idx,
	   lv_event_cb_t cb,
	   void *cb_data);

  ~Selector();
  lv_obj_t *get_container();
  lv_obj_t *get_selector();
  lv_obj_t *get_label();
  uint32_t get_selected_idx();
  void set_selected_idx(uint32_t idx);

  // Replace the options after construction. Used to rebuild from config and to
  // apply printer limits.
  void set_options(const std::vector<std::string> &opts, uint32_t default_idx);

  // Grey out an option the printer would reject. Disabled buttons stay visible
  // so it is obvious the value exists but is out of range.
  void set_enabled(uint32_t idx, bool enabled);

  // Number of real buttons, excluding "\n" separators and the terminator.
  uint32_t option_count() const;

  // Text of the current selection, or nullptr if the index does not point at a
  // button. lv_btnmatrix_get_btn_text returns NULL for an out of range index,
  // and passing that into fmt::format is undefined behaviour, so callers must
  // check this rather than using get_btn_text directly.
  const char *selected_text();

 private:
  void apply_options(const std::vector<std::string> &opts, uint32_t default_idx);

  lv_obj_t *cont;
  lv_obj_t *label;
  lv_obj_t *btnm;
  // owned holds the strings, map holds pointers into them.
  // lv_btnmatrix_set_map stores the array pointer and never copies, so both
  // must outlive the widget. owned is only ever assigned wholesale in
  // apply_options, never appended to afterwards, because reallocating it would
  // invalidate every pointer in map.
  std::vector<std::string> owned;
  std::vector<const char*> map;
  uint32_t selector_idx;
};

#endif //  __K_SELECTOR_H__
