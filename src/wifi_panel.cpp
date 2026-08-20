#include "wifi_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <utility>
#include <algorithm>

LV_IMG_DECLARE(back);

// LVGL's stock symbol page leaves ^, ~, | and ` unreachable, and all four are
// legal in a WPA passphrase, so a randomly generated one could simply not be
// typed. Upstream #146: a user with a ^ in their password had no way to get
// onto their network and no SSH to fall back on.
//
// This is LVGL's own default_kb_map_spec with a fifth row carrying the four,
// alongside the punctuation that was already reachable but on the other page,
// so a password can be typed without switching back and forth.
#define KB_BTN(w) (LV_BTNMATRIX_CTRL_POPOVER | (w))

// Not const-qualified on the pointers: lv_keyboard_set_map takes const char**
// rather than const char* const*, so the map has to match.
static const char *kb_map_spec[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
  "^", "~", "|", "`", "-", "_", ":", ".", ",", "\n",
  LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t kb_ctrl_spec_map[] = {
  KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
  KB_BTN(1), KB_BTN(1), KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | 2,

  LV_KEYBOARD_CTRL_BTN_FLAGS | 2, KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
  KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),

  KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
  KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),

  KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1), KB_BTN(1),
  KB_BTN(1), KB_BTN(1),

  LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
  LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static void draw_part_event_cb(lv_event_t * e)
{
  KGuard::event("WifiPanel draw_part_event_cb", [&] {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_ITEMS) {
      uint32_t row = dsc->id /  lv_table_get_col_cnt(obj);
      uint32_t col = dsc->id - row * lv_table_get_col_cnt(obj);

      if(col == 1) {
        dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;
      }
    }
  });
}

WifiPanel::WifiPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , top_cont(lv_obj_create(cont))
  , wifi_table(lv_table_create(top_cont))
  , wifi_right(lv_obj_create(top_cont))
  , prompt_cont(wifi_right)
  , wifi_label(lv_label_create(prompt_cont))
  , password_input(lv_textarea_create(prompt_cont))
  , forget_btn(lv_btn_create(prompt_cont))
  , back_btn(cont, &back, "Back", &WifiPanel::_handle_back_btn, this)
  , kb(lv_keyboard_create(cont))
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);  
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);
  
  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));
  
  lv_obj_set_height(wifi_table, LV_PCT(90));
  lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);

  auto screen_width = lv_disp_get_physical_hor_res(NULL) / 2 - 100;
  
  lv_table_set_col_width(wifi_table, 0, screen_width);
  lv_table_set_col_width(wifi_table, 1, 100);
  
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, draw_part_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  lv_obj_set_scroll_dir(wifi_table, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_set_style_border_width(wifi_right, 0, 0);
  lv_obj_set_flex_grow(wifi_right, 1);
  lv_obj_add_flag(wifi_right, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(prompt_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(prompt_cont, 0, 0);
  
  // Bounded and wrapping. It had no width at all, so anything longer than
  // "Enter password for X" ran off both edges of the panel, which the new
  // failure messages are.
  lv_obj_set_width(wifi_label, LV_PCT(90));
  lv_label_set_long_mode(wifi_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 70);

  lv_obj_set_size(password_input, LV_PCT(80), LV_SIZE_CONTENT);
  lv_textarea_set_password_mode(password_input, true);
  lv_textarea_set_one_line(password_input, true);

  // There was no way to forget a saved network at all. A password typed wrong
  // is stored by SAVE_CONFIG and retried forever, with no failure state and no
  // way to correct it, so upstream #156's reporter had to rename the network on
  // their router to get out of it. Shown only for a network wpa_supplicant
  // already has.
  lv_obj_add_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(forget_btn, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_add_event_cb(forget_btn, &WifiPanel::_handle_forget_btn, LV_EVENT_CLICKED, this);
  lv_obj_t *forget_label = lv_label_create(forget_btn);
  lv_label_set_text(forget_label, LV_SYMBOL_TRASH "  Forget");
  lv_obj_center(forget_label);

  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, kb_map_spec, kb_ctrl_spec_map);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_DEFOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_READY, this);

  // allow clicks on non-clickables to hide the keyboard
  lv_obj_add_event_cb(prompt_cont, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(wifi_label, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_move_background(cont);
  lv_obj_move_foreground(spinner);

  wpa_event.register_callback("WifiPanel",
      [this](const std::string &event) { this->handle_wpa_event(event); });

  wpa_event.start();

  // wpa events arrive on WpaEvent's own thread and are handled from here, which
  // is the same shape the websocket queue uses. 200ms because the only thing
  // waiting on one is a scan result behind a spinner.
  wpa_drain_timer = lv_timer_create(&WifiPanel::_drain_wpa, 200, this);
}

WifiPanel::~WifiPanel() {
  if (wpa_drain_timer != NULL) {
    lv_timer_del(wpa_drain_timer);
    wpa_drain_timer = NULL;
  }

  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void WifiPanel::foreground() {
  spdlog::trace("wifi panel fg");
  lv_obj_move_foreground(cont);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  wpa_event.send_command("SCAN");
}

void WifiPanel::handle_back_btn(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    spdlog::trace("wifi panel bg");
    lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

void WifiPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(wifi_table, &row, &col);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE) {
      return;
    }

    std::string previous = selected_network;
    selected_network = lv_table_get_cell_value(wifi_table, row, 0);

    // Anything already typed belongs to the network it was typed for. Without
    // this, half a password for one network stays in the box, and finishing it
    // against the next one sends the two concatenated.
    if (selected_network != previous) {
      lv_textarea_set_text(password_input, "");
    }

    // Offered for anything wpa_supplicant has a network block for, connected or
    // not, since a wrong password is exactly the case you need it for.
    if (list_networks.count(selected_network)) {
      lv_obj_clear_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
    }

    if (cur_network.length() > 0 && cur_network == selected_network) {
      auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
      lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
						selected_network,
						ip).c_str());
      lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      hide_keyboard();

    } else if (list_networks.count(selected_network)) {
      auto nid = list_networks.find(selected_network)->second;
      wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
      wpa_event.send_command("SAVE_CONFIG");
      lv_label_set_text(wifi_label,
			fmt::format("Connecting to {} ...", selected_network).c_str());
      lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      hide_keyboard();
    } else {
      lv_label_set_text(wifi_label, fmt::format("Enter password for {}", selected_network).c_str());
      lv_obj_clear_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      lv_event_send(password_input, LV_EVENT_FOCUSED, NULL);
    }

    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

// wpa_supplicant's way of saying the attempt failed. Only SCAN-RESULTS and
// CONNECTED were ever handled, so "Connecting to X ..." could be set and never
// cleared: a wrong password left the panel saying it was still trying, forever,
// which is the half of upstream #156 and #143 that makes the missing Forget
// button unrecoverable.
static const char *wpa_failure_reason(const std::string &event) {
  if (event.find("CTRL-EVENT-SSID-TEMP-DISABLED") != std::string::npos) {
    // wpa_supplicant emits this with reason=WRONG_KEY after enough failed
    // handshakes, and it is as close to "wrong password" as it gets.
    return event.find("WRONG_KEY") != std::string::npos
      ? "Wrong password."
      : "Could not authenticate.";
  }
  if (event.find("CTRL-EVENT-AUTH-REJECT") != std::string::npos) {
    return "The network rejected the authentication.";
  }
  if (event.find("CTRL-EVENT-ASSOC-REJECT") != std::string::npos) {
    return "The network refused the connection.";
  }
  if (event.find("CTRL-EVENT-NETWORK-NOT-FOUND") != std::string::npos) {
    return "Network not found.";
  }
  if (event.find("reason=WRONG_KEY") != std::string::npos) {
    return "Wrong password.";
  }
  return NULL;
}

void WifiPanel::handle_wpa_event(const std::string &event) {
  const char *failure = wpa_failure_reason(event);
  if (failure != NULL) {
    spdlog::debug("wifi attempt failed: {}", event);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wifi_label,
		      fmt::format("Could not connect to {}.\n{}\nForget it to try a new password.",
				  selected_network, failure).c_str());
    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
    if (list_networks.count(selected_network)) {
      lv_obj_clear_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (event.rfind("<3>CTRL-EVENT-SCAN-RESULTS", 0) == 0) {
    // result ready
    spdlog::trace("got scan result event");
    std::istringstream f(wpa_event.send_command("SCAN_RESULTS"));
    std::string line;
    wifi_name_db.clear();
    uint32_t index = 0;

    find_current_network();
    spdlog::trace("cur_network {}", cur_network);

    while (std::getline(f, line)) {
      if (line.rfind("bss", 0) == 0) {
	continue;
      }

      auto wifi_parts = KUtils::split(line, '\t');
      spdlog::trace("wifi parts {}", fmt::join(wifi_parts, ", "));
      if (wifi_parts.size() == 5) {
	auto inserted = wifi_name_db.insert({wifi_parts[4], std::stoi(wifi_parts[2])});
	if (inserted.second) {
	  lv_table_set_cell_value(wifi_table, index, 0, wifi_parts[4].c_str());
	  if (cur_network != wifi_parts[4]) {
	    spdlog::trace("adding symbol");
	    lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_WIFI);
	  } else {
	    spdlog::trace("adding symbol with ok");
	    lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_OK "    " LV_SYMBOL_WIFI);
	    auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
	    lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
						      cur_network,
						      ip).c_str());
	    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
	    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
	  }

	  index++;
	}
      }
    }
    lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  } else if (event.rfind("<3>CTRL-EVENT-CONNECTED", 0) == 0) {
    if (find_current_network()) {
      spdlog::trace("cur_network {}", cur_network);
      std::vector<std::pair<std::string, int>> pairs;
      for (auto it = wifi_name_db.begin(); it != wifi_name_db.end(); ++it) {
	pairs.push_back(*it);
      }
      
      std::sort(pairs.begin(), pairs.end(), [=](std::pair<std::string, int>& a,
						std::pair<std::string, int>& b)
      {
	return a.second > b.second;
      });
      

      uint32_t index = 0;
      for (const auto &wifi : pairs) {
	lv_table_set_cell_value(wifi_table, index, 0, wifi.first.c_str());
	if (cur_network != wifi.first) {
	  spdlog::trace("adding symbol");
	  lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_WIFI);
	} else {
	  spdlog::trace("adding symbol with ok");
	  lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_OK "    " LV_SYMBOL_WIFI);
	    auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
	    lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
						      cur_network,
						      ip).c_str());
	    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
	    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
	}
	index++;
      }

      lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// Put the keyboard away without going through the textarea's DEFOCUSED
// handler, which also rewrites wifi_label back to "Please select your wifi
// network". The callers below have just set that label to something they mean
// to keep.
void WifiPanel::hide_keyboard() {
  lv_keyboard_set_textarea(kb, NULL);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(password_input, LV_STATE_FOCUSED);
}

void WifiPanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, password_input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(kb, NULL);
    lv_label_set_text(wifi_label, "Please select your wifi network");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {
    const char *password = lv_textarea_get_text(password_input);
    if (password == NULL || password[0] == 0) {
      return;
    }

    // add network, set password, save wpa
    connect(password);
    lv_textarea_set_text(password_input, "");
    lv_label_set_text(wifi_label, fmt::format("Connecting to {} ...", selected_network).c_str());
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
    hide_keyboard();
  } else if (code == LV_EVENT_CLICKED) {
      lv_obj_t *target = lv_event_get_target(e);
      if (target != kb && target != password_input) {  
        lv_event_send(password_input, LV_EVENT_DEFOCUSED, NULL);
      }
  }
}

void WifiPanel::handle_forget_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  forget_network();
}

void WifiPanel::forget_network() {
  auto found = list_networks.find(selected_network);
  if (found == list_networks.end()) {
    return;
  }

  spdlog::debug("forgetting network {} (id {})", selected_network, found->second);
  wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", found->second));
  wpa_event.send_command("SAVE_CONFIG");

  list_networks.erase(found);
  if (cur_network == selected_network) {
    cur_network.clear();
  }

  lv_obj_add_flag(forget_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(wifi_label,
		    fmt::format("Forgot {}.\nSelect it again to enter a new password.",
				selected_network).c_str());
}

void WifiPanel::connect(const char *password) {
  // Replace rather than accumulate. ADD_NETWORK on every attempt meant retyping
  // a password left the old block behind, and wpa_supplicant kept trying both,
  // so wpa_supplicant.conf filled up with duplicates of one SSID.
  auto existing = list_networks.find(selected_network);
  if (existing != list_networks.end()) {
    wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", existing->second));
    list_networks.erase(existing);
  }

  std::string nid = wpa_event.send_command("ADD_NETWORK");
  spdlog::trace("add_network {}", nid);

  // The reply is the id followed by a newline, and it was interpolated raw into
  // the commands below. wpa_supplicant's atoi based parse survived it, which is
  // the only reason it worked.
  while (nid.size() > 0 && (nid.back() == '\n' || nid.back() == '\r')) {
    nid.pop_back();
  }

  if (nid.length() > 0) {
    wpa_event.send_command(fmt::format("SET_NETWORK {} ssid {:?}", nid, selected_network));
    wpa_event.send_command(fmt::format("SET_NETWORK {} psk {:?}", nid, password));
    wpa_event.send_command(fmt::format("ENABLE_NETWORK {}", nid));
    wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
    wpa_event.send_command("SAVE_CONFIG");
    list_networks.insert({selected_network, nid});
  }
}

bool WifiPanel::find_current_network() {
  list_networks.clear();
  std::string nets = wpa_event.send_command("LIST_NETWORKS");
  spdlog::trace("nets = {}", nets);
  std::istringstream f(nets);
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    auto wifi_parts = KUtils::split(line, '\t');
    if (wifi_parts.size() == 4 && line.find("[CURRENT]") != std::string::npos) {
	cur_network = wifi_parts[1];
	list_networks.insert({wifi_parts[1], wifi_parts[0]});
	found = true;
    }

    if (wifi_parts.size() > 1) {
      list_networks.insert({wifi_parts[1], wifi_parts[0]});
    }
  }

  return found;
}
