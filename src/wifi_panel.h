#ifndef __WIFI_PANEL_H__
#define __WIFI_PANEL_H__

#include "wpa_event.h"
#include "event_guard.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <map>
#include <set>
#include <string>

class WifiPanel {
 public:
  WifiPanel();
  
  ~WifiPanel();

  void foreground();
  void handle_back_btn(lv_event_t *event);
  void handle_callback(lv_event_t *event);
  void handle_wpa_event(const std::string &events);
  void handle_kb_input(lv_event_t *e);
  void connect(const char *);
  void hide_keyboard();
  bool find_current_network();
  void forget_network();
  void handle_forget_btn(lv_event_t *event);

  static void _handle_back_btn(lv_event_t *event) {
    KGuard::event("WifiPanel::_handle_back_btn", [&] {
      WifiPanel *panel = (WifiPanel*)event->user_data;
      panel->handle_back_btn(event);
    });
  };
  
  static void _handle_callback(lv_event_t *event) {
    KGuard::event("WifiPanel::_handle_callback", [&] {
      WifiPanel *panel = (WifiPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };
  
  static void _handle_forget_btn(lv_event_t *event) {
    KGuard::event("WifiPanel::_handle_forget_btn", [&] {
      WifiPanel *panel = (WifiPanel*)event->user_data;
      panel->handle_forget_btn(event);
    });
  };

  static void _handle_kb_input(lv_event_t *e) {
    KGuard::event("WifiPanel::_handle_kb_input", [&] {
      WifiPanel *panel = (WifiPanel*)e->user_data;
      panel->handle_kb_input(e);
    });
  };

  // Pulls whatever wpa_supplicant has said onto this thread. KGuard::event is
  // inside WpaEvent::drain, per callback, rather than here.
  static void _drain_wpa(lv_timer_t *timer) {
    WifiPanel *panel = (WifiPanel*)timer->user_data;
    panel->wpa_event.drain();
  };

 private:
  WpaEvent wpa_event;
  lv_obj_t *cont;
  lv_obj_t *spinner;
  lv_obj_t *top_cont;
  lv_obj_t *wifi_table;
  lv_obj_t *wifi_right;
  lv_obj_t *prompt_cont;
  lv_obj_t *wifi_label;
  lv_obj_t *password_input;
  lv_obj_t *forget_btn;
  ButtonContainer back_btn;
  lv_obj_t *kb;
  std::string selected_network;
  std::string cur_network;
  std::map<std::string, std::string> list_networks;
  std::map<std::string, int> wifi_name_db;
  lv_timer_t *wpa_drain_timer;
  
};

#endif // __WIFI_PANEL_H__
